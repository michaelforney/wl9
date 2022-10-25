.POSIX:

CFLAGS+=-std=c11 -Wall -Wpedantic -Wno-parentheses
CFLAGS+=-D C9_NO_SERVER
LIBS+=-lwayland-server

-include config.mk

OBJ=\
	wl9.o\
	c9.o\
	fs.o\
	util.o\
	keymap.o\
	xdg-shell-protocol.o\
	server-decoration-protocol.o\

HDR=\
	arg.h\
	c9.h\
	fs.h\
	keymap.h\
	server-decoration-server-protocol.h\
	util.h\
	xdg-shell-client-protocol.h\
	xdg-shell-server-protocol.h\


.PHONY: all
all: wl9

$(OBJ): $(HDR)

wl9: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

kbmaptoxkb: kbmaptoxkb.o keymap.o
	$(CC) $(LDFLAGS) -o $@ kbmaptoxkb.o keymap.o

.PHONY: clean
clean:
	rm -f wl9 $(OBJ) kbmaptoxkb kbmaptoxkb.o
