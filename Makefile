GITREV=$(shell ./getgitrev)

ringsip: ringsip.cpp
	g++ -Wall -std=c++11 -DGITREV="\"$(GITREV)\"" -o ringsip ringsip.cpp `pkg-config --cflags --libs libpjproject`

clean::
	-rm ringsip

install:: ringsip
	mkdir -p /opt/ringsip/bin
	cp ringsip /opt/ringsip/bin

