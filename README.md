# ringsip
Rings a phone via SIP (e.g. FRITZ!Box)

    This program registers with a SIP registrar, makes a call and then hangs up
    Version: d1cfa4a (dirty)

    usage: ringsip [OPTIONS] <registrar> <username> <password> <callee>

    registrar:     The IP address or host name of the SIP registrar
    username:      User name for login at the registrar
    password:      Password for the login
    callee:        Phone number or sip URI to call

    Options:
    --duration n:  Ring for n seconds (default: 5)
    --name str:    Use str as caller name
    -v, -vv, -vvv: Little to medium verbosity
    -vvvv:         Also show SIP messages
    -vvvvv, ...:   Be very verbose

    Examples:
    ringsip --name "Hello world" fritz.box 620 secret '**701'
    ringsip --duration 20 192.168.0.1 620 secret 01711234567
    ringsip 192.168.0.1 620 secret sip:pete@host.com

## Building

Currently, only Debian Jessie is tested.

In order to build, do the following:

    apt-get install libpjproject-dev
    make

`make install` will copy the executable to `/opt/ringsip/bin`, but you can copy it wherever you want.

