struct rect {
	int x0, y0;
	int x1, y1;
};

int drawgpuinit(void);
int draw9pinit(void);

/* prepare for drawing to the given window */
extern int (*drawattach)(struct wl_resource *win, const char *name, int w, int h);

/* draw to rect r in window from point (x, y) in buffer */
extern int (*draw)(struct wl_resource *win, struct rect r, struct wl_resource *buf, int x, int y);

extern struct rect drawrect;

/* emitted with the window resource when drawing is finished */
extern struct wl_signal drawdone;
