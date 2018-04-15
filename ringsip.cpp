/* ringsip.cpp
 * Copyright (C) 2017 Florian Kusche <git@kusche.de>
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
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <atomic>
#include <unistd.h>
#include <signal.h>

#define MAX_PASSWORD    50
#define THIS_FILE	"APP"

std::atomic<int> g_registerState( 0 );          // 0: not registered yet, -1 registration failed, 1 registration ok
std::atomic<bool> g_stop( false );              // true: signal (HUP, INT, ...) received

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
                     "               If this starts with '/', the password will be read from that file\n"
                     "callee:        Phone number or sip URI to call\n\n"
                     "Options:\n"
                     "--duration n:  Ring for n seconds (default: 5)\n"
                     "--name str:    Use str as caller name\n"
                     "-v, -vv, -vvv: Little to medium verbosity\n"
                     "-vvvv:         Also show SIP messages\n"
                     "-vvvvv, ...:   Be very verbose\n\n"
                     "Examples:\n"
                     "ringsip --name \"Hello world\" fritz.box 620 secret '**701'\n"
                     "ringsip --duration 20 192.168.0.1 620 secret 01711234567\n"
                     "ringsip 192.168.0.1 620 secret sip:pete@host.com\n",
                     text ? "Error: " : "", text ? text : "", text ? "\n\n" : "", GITREV );
    exit( 1 );
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
    const char *name = nullptr, *registrar, *username, *password, *callee;
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

    // do some validation
    if( name && strlen( name ) > 100 )
        usage( "name too long" );
    if( strlen( registrar ) > 100 )
        usage( "registrar too long" );
    if( strlen( username ) > 50 )
        usage( "username too long" );
    if( strlen( password ) > 50 )
        usage( "password too long" );
    if( strlen( callee ) > 100 )
        usage( "callee too long" );

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
	pjsua_transport_config cfg;

	pjsua_transport_config_default(&cfg);
	cfg.port = 5060;
	status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, NULL);
	if (status != PJ_SUCCESS) error_exit("Error creating transport", status);
    }

    /* Initialization is done, now start pjsua */
    status = pjsua_start();
    if (status != PJ_SUCCESS) error_exit("Error starting pjsua", status);

    signal( SIGHUP, signalHandler );
    signal( SIGINT, signalHandler );
    signal( SIGTERM, signalHandler );

    /* Register to SIP server by creating SIP account. */
    {
	pjsua_acc_config cfg;

	pjsua_acc_config_default(&cfg);

        char id[512], reg_uri[512];

        if( name and name[0] ) {
            // remove bad chars
            int dest = 0;
            id[dest++] = '"';
            for( int src = 0; name[src]; src++ )
                if( ( unsigned char ) name[src] >= 32 && name[src] != '"' )
                    id[dest++] = name[src];

            sprintf( id + dest, "\" <sip:%s@%s>", username, registrar );
        } else
            sprintf( id, "sip:%s@%s", username, registrar );

        sprintf( reg_uri, "sip:%s", registrar );

        // the const casts are ok here. They are because of the dreaded pj_str,
        // that needs non-const pointers. However, pjsua_acc_add will copy them and never change them.

	cfg.id = pj_str( id );
	cfg.reg_uri = pj_str( reg_uri );
	cfg.cred_count = 1;
	cfg.cred_info[0].realm = pj_str(const_cast<char*>( "*" ));
	cfg.cred_info[0].scheme = pj_str(const_cast<char*>( "digest" ));
	cfg.cred_info[0].username = pj_str(const_cast<char*>( username ));
	cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	cfg.cred_info[0].data = pj_str(const_cast<char*>( password ));

	status = pjsua_acc_add(&cfg, PJ_TRUE, &acc_id);
	if (status != PJ_SUCCESS) error_exit("Error adding account", status);
    }

    /* wait until registered */

    while( g_registerState.load() == 0 && !g_stop.load())
        usleep( 100000 );

    if( g_stop.load())
        error_exit( "Stopped by signal", PJ_EUNKNOWN );

    if( g_registerState.load() != 1 )
        error_exit( "Error registering at registrar", PJ_EUNKNOWN );

    // now make the call

    {
        char buf[512];

        if( callee[0] == 's' && callee[1] == 'i' && callee[2] == 'p' && callee[3] == ':' )
            strcpy( buf, callee );
        else
            sprintf( buf, "sip:%s@%s", callee, registrar );

        pj_str_t uri = pj_str(buf);
        status = pjsua_call_make_call(acc_id, &uri, 0, NULL, NULL, NULL);
        if (status != PJ_SUCCESS) error_exit("Error making call", status);
    }

    // wait for duration
    time_t endtime = time( NULL ) + duration;
    while( !g_stop.load() && time( NULL ) < endtime )
        usleep( 100000 );

    pjsua_call_hangup_all();

    /* Destroy pjsua */
    pjsua_destroy();

    return 0;
}
