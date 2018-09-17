# ringsip
Rings a phone via SIP (e.g. FRITZ!Box)

    This program registers with a SIP registrar, makes a call and then hangs up
    Version: dda4a7a (clean)

    usage: ringsip [OPTIONS] <registrar> <username> <password> <callee>

    registrar:     The IP address or host name of the SIP registrar
    username:      User name for login at the registrar
    password:      Password for the login
                   If this starts with '/', the password will be read from that file.
                   This is the recommended way, because then, the password won't be visible in the task list.
    callee:        Phone number or sip URI to call

    Options:
    --duration n:  Ring for n seconds (default: 5)
    --name str:    Use str as caller name
    --daemon fifo: Daemonize, in order to actually ring, send a string to the FIFO file.
                   If you just send newline, it will not change the name.
                   If you send a text followed by newline, it will change the caller name before ringing.
    -v, -vv, -vvv: Little to medium verbosity
    -vvvv:         Also show SIP messages
    -vvvvv, ...:   Be very verbose

    Examples:
    ringsip --name "Hello world" fritz.box 620 secret '**701'
    ringsip --duration 20 192.168.0.1 620 secret 01711234567
    ringsip 192.168.0.1 620 secret sip:pete@host.com
    mkfifo /run/ringsip.fifo
    ringsip --daemon /run/ringsip.fifo 192.168.0.1 620 /etc/ringsip.password **701

## Building

Currently, only Debian Jessie and Stretch are tested.

In order to build, do the following:

    sudo apt-get install libpjproject-dev libsrtp-dev
    make

`make install` will copy the executable to `/opt/ringsip/bin`, but you can copy it wherever you want.

## Sample systemd unit

If you want to run ringsip as a service, you can create a file called `/etc/systemd/system/ringsip.service`
with contents like this:

```
[Unit]
Description=RingSIP
After=network-online.target

[Service]
ExecStartPre=-/usr/bin/mkfifo /run/ringsip.fifo
ExecStart=/opt/ringsip/bin/ringsip --daemon /run/ringsip.fifo --duration 8 192.168.0.1 620 /my/file/with/sip-password **701
Type=simple
#User=foo
#Group=bar

[Install]
WantedBy=multi-user.target
```

