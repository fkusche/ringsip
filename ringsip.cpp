/* ringsip.cpp
 * Copyright (C) 2017 Florian Kusche <ringsip@kusche.de>
 *
 * based on simple_pjsua which is
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#include <pjsua-lib/pjsua.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <unistd.h>
#include <signal.h>

#define THIS_FILE	"APP"
#define MAX_PASSWORD    50

std::atomic<int> g_registerState( 0 );          // 0: not registered yet, -1 registration failed, 1 registration ok
std::atomic<bool> g_stop( false );              // true: signal (HUP, INT, ...) received

pjsua_acc_config g_acc_cfg;
char g_acc_cfg_id[500];                         // the buffer behind the g_acc_cfg.id field
char g_username[101];
char g_registrar[101];
char g_callee[250];                             // must be at least 100 (max length of callee on cmdline) + max length of registrar + some chars

/* Callback called by the library upon receiving incoming call */
static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
			     pjsip_rx_data *rdata)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);

    pjsua_call_get_info(call_id, &ci);

    PJ_LOG(3,(THIS_FILE, "Incoming call from %.*s!!",
			 (int)ci.remote_info.slen,
			 ci.remote_info.ptr));

    /* Automatically answer incoming calls with 200/OK */
    pjsua_call_answer(call_id, 200, NULL, NULL);
}

/* Callback called by the library when call's state has changed */
static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(e);

    pjsua_call_get_info(call_id, &ci);
    PJ_LOG(3,(THIS_FILE, "Call %d state=%.*s", call_id,
			 (int)ci.state_text.slen,
			 ci.state_text.ptr));
}

/* Callback called by the library when call's media state has changed */
static void on_call_media_state(pjsua_call_id call_id)
{
    pjsua_call_info ci;

    pjsua_call_get_info(call_id, &ci);

    if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
	// When media is active, connect call to sound device.
	pjsua_conf_connect(ci.conf_slot, 0);
	pjsua_conf_connect(0, ci.conf_slot);
    }
}

static void on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info *info)
{
    PJ_LOG(1,(THIS_FILE, "%sregistration result: status=%s, code=%d, reason=%.*s", 
                info->cbparam->expiration == 0 ? "un" : "",
                info->cbparam->status == PJ_SUCCESS ? "success" : "failed",
                info->cbparam->code, 
                (int) info->cbparam->reason.slen, info->cbparam->reason.ptr ));

    if( info->cbparam->status == PJ_SUCCESS && info->cbparam->code >= 200 && info->cbparam->code <= 299 )
        g_registerState.store( 1 );
    else
        g_registerState.store( -1 );
}



/* Display error and exit application */
static void error_exit(const char *title, pj_status_t status)
{
    fprintf( stderr, "\n%s\n", title );
    pjsua_perror(THIS_FILE, title, status);
    pjsua_destroy();
    exit(1);
}

static void signalHandler( int signal )
{
    PJ_LOG( 1, (THIS_FILE, "Signal %d received.", signal ));

    g_stop.store( true );
}

static void usage( const char* text = nullptr )
{
    fprintf( stderr, "%s%s%sThis program registers with a SIP registrar, makes a call and then hangs up\n"
                     "Version: %s\n\n"
                     "usage: ringsip [OPTIONS] <registrar> <username> <password> <callee>\n\n"
                     "registrar:     The IP address or host name of the SIP registrar\n"
                     "username:      User name for login at the registrar\n"
                     "password:      Password for the login\n"
                     "               If this starts with '/', the password will be read from that file.\n"
                     "               This is the recommended way, because then, the password won't be visible in the task list.\n"
                     "callee:        Phone number or sip URI to call\n\n"
                     "Options:\n"
                     "--duration n:  Ring for n seconds (default: 5)\n"
                     "--name str:    Use str as caller name\n"
                     "--daemon fifo: Daemonize, in order to actually ring, send a string to the FIFO file.\n"
                     "               If you just send newline, it will not change the name.\n"
                     "               If you send a text followed by newline, it will change the caller name before ringing.\n"
                     "-v, -vv, -vvv: Little to medium verbosity\n"
                     "-vvvv:         Also show SIP messages\n"
                     "-vvvvv, ...:   Be very verbose\n\n"
                     "Examples:\n"
                     "ringsip --name \"Hello world\" fritz.box 620 secret '**701'\n"
                     "ringsip --duration 20 192.168.0.1 620 secret 01711234567\n"
                     "ringsip 192.168.0.1 620 secret sip:pete@host.com\n"
                     "mkfifo /run/ringsip.fifo\n"
                     "ringsip --daemon /run/ringsip.fifo 192.168.0.1 620 /etc/ringsip.password **701\n",
                     text ? "Error: " : "", text ? text : "", text ? "\n\n" : "", GITREV );
    exit( 1 );
}

// This will set g_acc_cfg.id to the correct value
static void setID( const char* name )
{
    if( name and name[0] ) {
        // remove bad chars
        int dest = 0;
        g_acc_cfg_id[dest++] = '"';
        for( int src = 0; name[src]; src++ )
            if( ( unsigned char ) name[src] >= 32 && name[src] != '"' )
                g_acc_cfg_id[dest++] = name[src];

        sprintf( g_acc_cfg_id + dest, "\" <sip:%s@%s>", g_username, g_registrar );
    } else
        sprintf( g_acc_cfg_id, "sip:%s@%s", g_username, g_registrar );

    g_acc_cfg.id = pj_str( g_acc_cfg_id );
}


static void ring( pjsua_acc_id acc_id, int duration )
{
    pj_status_t status;

    pj_str_t pjCallee = pj_str(g_callee);

    status = pjsua_call_make_call(acc_id, &pjCallee, 0, NULL, NULL, NULL);
    if (status != PJ_SUCCESS) error_exit("Error making call", status);

    // wait for duration
    time_t endtime = time( NULL ) + duration;
    while( !g_stop.load() && time( NULL ) < endtime )
        usleep( 100000 );

    pjsua_call_hangup_all();
}

/*
 * main()
 */
int main(int argc, char *argv[])
{
    // parse command line
    if( argc <= 1 )
        usage();

    int pos = 1;
    int duration = 5;
    int loglevel = 0;
    int daemon_handle = 0;
    const char *name = nullptr, *registrar, *username, *password, *callee, *daemon_fifo = nullptr;

    for( ; pos < argc; pos++ ) {
        if( argv[pos][0] != '-' ) {
            break;
        } else if( !strcmp( argv[pos], "--duration" ) && pos + 1 < argc ) {
            duration = atoi( argv[++pos] );
            if( duration < 1 )
                usage( "wrong duration" );
        } else if( !strcmp( argv[pos], "--name" ) && pos + 1 < argc ) {
            name = argv[++pos];
        } else if( argv[pos][0] == '-' && argv[pos][1] == 'v' ) {
            // count the v's
            for( loglevel = 0; argv[pos][loglevel+1] == 'v'; loglevel++ );
            // must be at the end of the string
            if( argv[pos][loglevel+1] )
                usage( "unknown option" );
        } else if( !strcmp( argv[pos], "--daemon" )) {
            daemon_fifo = argv[++pos];
        } else {
            usage( "unknown option" );
        }
    }

    // must have exactly 4 args left
    if( pos + 4 != argc )
        usage( "missing mandatory arguments" );

    registrar = argv[pos];
    username  = argv[pos+1];
    password  = argv[pos+2];
    callee    = argv[pos+3];

    if( password[0] == '/' ) {
        int handle = open( password, O_RDONLY );
        if( handle < 0 ) {
            fprintf( stderr, "error reading password file %s", password );
            return 1;
        }
        char* buf = new char[MAX_PASSWORD + 2];
        int n = read( handle, buf, MAX_PASSWORD + 1 );
        buf[MAX_PASSWORD+1] = 0;
        // truncate at first non-printable character
        for( int i = 0; i < n; i++ )
            if( (unsigned char) buf[i] < 32 )
                buf[i] = 0;
        password = buf;
    }

    if( daemon_fifo ) {
        // We have to open the FIFO non-blocking, because otherwise, open() would block
        // until there is a process on the other end.
        // Then, we immediately set the FIFO to blocking.
        daemon_handle = open( daemon_fifo, O_RDONLY | O_NONBLOCK );
        if( daemon_handle < 0 ) {
            fprintf( stderr, "error opening fifo file %s", daemon_fifo );
            return 1;
        }
        int flags = fcntl(daemon_handle, F_GETFL, 0);
        if( flags == -1 || fcntl(daemon_handle, F_SETFL, flags & ~O_NONBLOCK) == -1 ) {
            fprintf( stderr, "error setting fifo to blocking" );
            return 1;
        }
    }

    // do some validation
    if( name && strlen( name ) > 100 )
        usage( "name too long" );
    if( strlen( registrar ) > sizeof( g_registrar ) - 1 )
        usage( "registrar too long" );
    if( strlen( username ) > sizeof( g_username ) - 1 )
        usage( "username too long" );
    if( strlen( password ) > MAX_PASSWORD )
        usage( "password too long" );
    if( strlen( callee ) > 100 )
        usage( "callee too long" );

    strcpy( g_username, username );
    strcpy( g_registrar, registrar );

    if( callee[0] == 's' && callee[1] == 'i' && callee[2] == 'p' && callee[3] == ':' )
        strcpy( g_callee, callee );
    else
        sprintf( g_callee, "sip:%s@%s", callee, registrar );

    pjsua_acc_id acc_id;
    pj_status_t status;

    // Unfortunately, ALSA outputs some error messages on stderr.
    // Maybe this could be suppressed by using snd_lib_error_set_handler().
    // However, then we would have a dependency on ALSA here.
    // So we don't do this at the moment.

    /* Create pjsua first! */
    status = pjsua_create();
    if (status != PJ_SUCCESS) error_exit("Error in pjsua_create()", status);

    /* Init pjsua */
    {
	pjsua_config cfg;
	pjsua_logging_config log_cfg;

	pjsua_config_default(&cfg);
	cfg.cb.on_incoming_call = &on_incoming_call;
	cfg.cb.on_call_media_state = &on_call_media_state;
	cfg.cb.on_call_state = &on_call_state;
        cfg.cb.on_reg_state2 = &on_reg_state2;

	pjsua_logging_config_default(&log_cfg);
	log_cfg.console_level = loglevel;

	status = pjsua_init(&cfg, &log_cfg, NULL);
	if (status != PJ_SUCCESS) error_exit("Error in pjsua_init()", status);
    }

    pjsua_set_null_snd_dev();

    /* Add UDP transport. */
    {
	pjsua_transport_config t_cfg;

	pjsua_transport_config_default(&t_cfg);
	t_cfg.port = 5060;
	status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &t_cfg, NULL);
	if (status != PJ_SUCCESS) error_exit("Error creating transport", status);
    }

    /* Initialization is done, now start pjsua */
    status = pjsua_start();
    if (status != PJ_SUCCESS) error_exit("Error starting pjsua", status);

    signal( SIGHUP, signalHandler );
    signal( SIGINT, signalHandler );
    signal( SIGTERM, signalHandler );

    pjsua_acc_config_default(&g_acc_cfg);

    char reg_uri[512];
    setID( name );

    sprintf( reg_uri, "sip:%s", registrar );

    // the const casts are ok here. They are because of the dreaded pj_str,
    // that needs non-const pointers. However, pjsua_acc_add will copy them and never change them.
    // (It's also done implicitly in the simple_pjsua.c example that way.)

    g_acc_cfg.reg_uri = pj_str( reg_uri );
    g_acc_cfg.cred_count = 1;
    g_acc_cfg.cred_info[0].realm = pj_str(const_cast<char*>( "*" ));
    g_acc_cfg.cred_info[0].scheme = pj_str(const_cast<char*>( "digest" ));
    g_acc_cfg.cred_info[0].username = pj_str( g_username );
    g_acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
    g_acc_cfg.cred_info[0].data = pj_str(const_cast<char*>( password ));

    status = pjsua_acc_add(&g_acc_cfg, PJ_TRUE, &acc_id);
    if (status != PJ_SUCCESS) error_exit("Error adding account", status);

    /* wait until registered */

    while( g_registerState.load() == 0 && !g_stop.load())
        usleep( 100000 );

    if( g_stop.load())
        error_exit( "Stopped by signal", PJ_EUNKNOWN );

    if( g_registerState.load() != 1 )
        error_exit( "Error registering at registrar", PJ_EUNKNOWN );

    // now make the call
    if( !daemon_handle ) {
        ring( acc_id, duration );
    } else {
        char text[100];
        int pos = 0;
        while( !g_stop.load()) {
            usleep( 100000 );

            int n = read( daemon_handle, text + pos, sizeof text - pos - 1 );
            if( n <= 0 )
                continue;

            pos += n;
            text[pos] = 0;
            // Is there a newline? (Should almost always be the case)
            char* nl = strchr( text, '\n' );
            if( !nl ) {
                // ok, someone sends characters individually
                if( pos >= (int) sizeof text - 1 )
                    pos = 0;                    // Someone sends large text. Ignore it
                continue;         
            }

            *nl = 0;            // discard everything after the newline. (There should not be anything anyway... Except someone sends multiple lines which is not very useful)

            setID( text );
            status = pjsua_acc_modify(acc_id, &g_acc_cfg);
            if (status != PJ_SUCCESS) error_exit("Error modifying account", status);

            ring( acc_id, duration );
            pos = 0;
        }
    }


    /* Destroy pjsua */
    pjsua_destroy();

    return 0;
}
