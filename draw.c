#include <wayland-server-core.h>
#include "draw.h"

int (*drawattach)(struct wl_resource *, const char *, int, int);
int (*draw)(struct wl_resource *, struct rect, struct wl_resource *, int, int);

extern struct rect drawrect;

struct wl_signal drawdone;
