CFLAGS+=-std=c11 -Wall -Wpedantic -Wno-parentheses
CFLAGS+=-D C9_NO_SERVER
LIBS+=-lwayland-server
drmtest_LIBS+=-ldrm

-include config.mk

HAVE_VIRTGPU=1
CFLAGS-$(HAVE_VIRTGPU)+=-D HAVE_VIRTGPU
LIBS-$(HAVE_VIRTGPU)+=-l drm
OBJ-$(HAVE_VIRTGPU)=draw-gpu.o

CFLAGS+=$(CFLAGS-1)
LIBS+=$(LIBS-1)

OBJ=\
	wl9.o\
	c9.o\
	draw.o\
	fs.o\
	util.o\
	keymap.o\
	xdg-shell-protocol.o\
	server-decoration-protocol.o\
	$(OBJ-1)

HDR=\
	arg.h\
	c9.h\
	draw.h\
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

drmtest: drmtest.o
	$(CC) $(LDFLAGS) -o $@ drmtest.o $(drmtest_LIBS)

.PHONY: clean
clean:
	rm -f wl9 $(OBJ) kbmaptoxkb kbmaptoxkb.o
