/* SPDX-License-Identifier: ISC */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server.h>
#include "arg.h"
#include "c9.h"
#include "keymap.h"
#include "util.h"
#include "fs.h"
#include "xdg-shell-server-protocol.h"
#include "server-decoration-server-protocol.h"

#define BORDER 4

struct damage {
	int x0, y0;
	int x1, y1;
};

struct surface_state {
	struct wl_resource *buffer;
	struct wl_listener buffer_destroy;
	struct wl_list callbacks;
	struct damage damage;
};

struct surface {
	struct wl_resource *resource;
	struct wl_resource *role;
	void (*commit)(struct surface *);
	struct surface_state pending, state;
};

struct region {
	struct wl_resource *resource;
};

struct window {
	struct surface *surface;
	struct wl_resource *xdgsurface;
	struct wl_resource *toplevel;
	struct wl_listener surface_destroy;
	uint32_t serial;
	int initial_commit;

	char name[32];
	int x0, y0;
	int x1, y1;
	int button;
	int mousex, mousey;
	struct wl_array keys;
	int current;
	int hidden;

	/* wsys fids */
	int wsys;
	int wctl;
	int winname;
	int label;
	int mouse;
	int kbd;

	/* pending i/o tags */
	C9tag wctltag;
	C9tag mousetag;
	C9tag kbdtag;

	/* /dev/draw image id */
	int image;
};

struct drawcopy {
	struct window *w;
	struct wl_shm_buffer *b;
	struct damage d;
	int x, y;
	int dx, dy;
};

struct snarfput {
	int snarf;
	unsigned long long offset;
	struct wl_resource *source;
	struct wl_event_source *event;
};

struct snarfget {
	int fd;
	unsigned long long offset;
	struct wl_resource *offer;
};

static struct wl_display *dpy;
static struct wl_event_loop *evt;
static struct wl_client *child;
static const struct wl_data_offer_interface offer_impl;
static struct {
	struct wl_list resources;
} output;
static struct {
	struct wl_list resources;
} datadev;
static struct {
	struct window *focus;
	struct wl_list active;
	struct wl_list inactive;
} mouse;
static struct {
	struct window *focus;
	struct wl_list active;
	struct wl_list inactive;
	uint32_t mods;
	int keymapfd;
	size_t keymapsize;
} kbd;
static struct {
	/* root fid */
	int root;
	/* $wsys fid */
	int wsys;
	/* /dev/snarf fid */
	int snarf;

	struct wl_event_source *event;
	uint32_t eventmask;
} term;
static struct {
	int ctlfid;
	int datafid;
	int datafd;

	int x0, y0;
	int x1, y1;
	unsigned char *buf;
	size_t buflen;
	struct numtab imgid;
} draw;

static C9aux termaux;
static C9ctx termctx;

static void
drawcopy(struct drawcopy *d)
{
	int x, y, x0, y0, x1, y1;
	unsigned char *img, *buf, *pos;
	size_t n, stride;
	int done;
	C9tag tag;
	struct wl_resource *r, *tmp;

	x = d->x, y = d->y;
	stride = wl_shm_buffer_get_stride(d->b);
	img = wl_shm_buffer_get_data(d->b);
	img += x * 4 + y * stride;
	buf = draw.buf;
	*buf++ = 'y';
	buf = putle32(buf, d->w->image);
	tag = -1;
	for (done = 0; !done;) {
		x0 = x;
		x1 = x + d->dx;
		if (x1 > d->d.x1)
			x1 = d->d.x1;

		y0 = y;
		y1 = y + d->dy;
		if (y1 > d->d.y1)
			y1 = d->d.y1;

		pos = putle32(buf, d->w->x0 + x0);
		pos = putle32(pos, d->w->y0 + y0);
		pos = putle32(pos, d->w->x0 + x1);
		pos = putle32(pos, d->w->y0 + y1);
		n = (x1 - x0) * 4;
		for (; y < y1; ++y)
			memcpy(pos, img, n), pos += n, img += stride;
		if (x1 < d->d.x1)
			x = x1, --y;
		else
			x = d->d.x0;
		if (y == d->d.y1) {
			done = 1;
			*pos++ = 'v';
		}

		if (fswrite(&termctx, &tag, draw.datafid, 0, draw.buf, pos - draw.buf) != 0) {
			fprintf(stderr, "fswrite %s draw: %s\n", d->w->name, termaux.err);
			break;
		}
	}
	assert(tag != -1);
	free(fswait(&termctx, tag, Rwrite));

	d->x = x;
	d->y = y;
	d->w->surface->pending.damage.x0 = -1;
	d->w->surface->pending.damage.y0 = -1;
	d->w->surface->pending.damage.x1 = -1;
	d->w->surface->pending.damage.y1 = -1;
	wl_resource_for_each_safe(r, tmp, &d->w->surface->state.callbacks) {
		wl_callback_send_done(r, 0);
		wl_resource_destroy(r);
	}
}

static int
snarfput(int fd, uint32_t mask, void *data)
{
	struct snarfput *put;
	char buf[8192];
	size_t len;
	ssize_t ret;

	put = data;
	len = sizeof buf;
	if (termctx.msize < len)
		len = termctx.msize - IOHDRSZ;
	ret = read(fd, buf, len);
	if (ret <= 0) {
		if (ret < 0)
			perror("read wl_data_source");
		close(fd);
		fsclunk(&termctx, put->snarf);
		wl_event_source_remove(put->event);
		wl_data_source_send_cancelled(put->source);
		free(put);
		return 0;
	}
	fswrite(&termctx, NULL, put->snarf, put->offset, buf, ret);
	put->offset += ret;
	return 0;
}

static void
snarfget(C9r *reply, void *aux)
{
	struct snarfget *get;
	C9tag tag;
	unsigned char *pos, *end;
	ssize_t ret;

	get = aux;
	if (reply->type == Rerror) {
		fprintf(stderr, "read snarf: %s\n", reply->error);
		goto free;
	}
	if (reply->read.size == 0) {
		goto free;
	}
	get->offset += reply->read.size;
	pos = reply->read.data;
	end = pos + reply->read.size;
	while (pos < end) {
		ret = write(get->fd, pos, end - pos);
		if (ret < 0) {
			perror("write wl_data_offer");
			goto free;
		}
		pos += ret;
	}
	if (fsread(&termctx, &tag, NULL, term.snarf, get->offset, 1024) != 0)
		goto free;
	fsasync(&termctx, tag, snarfget, get);
	return;

free:
	close(get->fd);
	free(get);
}

static void
snarfoffer(struct window *w)
{
	struct wl_resource *r, *offer;
	struct wl_client *c;
	uint32_t ver;

	c = wl_resource_get_client(w->xdgsurface);
	r = wl_resource_find_for_client(&datadev.resources, c);
	if (!r)
		return;
	ver = wl_resource_get_version(r);
	offer = wl_resource_create(c, &wl_data_offer_interface, ver, 0);
	if (offer) {
		wl_resource_set_implementation(offer, &offer_impl, NULL, NULL);
		wl_resource_set_user_data(r, offer);
		wl_data_device_send_data_offer(r, offer);
		wl_data_offer_send_offer(offer, "text/plain;charset=utf-8");
		wl_data_device_send_selection(r, offer);
	}
}

static void
addstate(struct wl_array *a, uint32_t s)
{
	uint32_t *p;

	p = wl_array_add(a, sizeof s);
	if (p)
		*p = s;
}

static void
mousefocus(struct window *w, int x, int y)
{
	struct wl_client *c;
	struct wl_resource *r, *s, *tmp;
	uint32_t serial;

	serial = wl_display_next_serial(dpy);
	if (mouse.focus) {
		s = mouse.focus->surface->resource;
		wl_resource_for_each(r, &mouse.active)
			wl_pointer_send_leave(r, serial, s);
		wl_list_insert_list(&mouse.inactive, &mouse.active);
		wl_list_init(&mouse.active);
	}
	if (w) {
		c = wl_resource_get_client(w->xdgsurface);
		s = w->surface->resource;
		wl_resource_for_each_safe(r, tmp, &mouse.inactive) {
			if (wl_resource_get_client(r) != c)
				continue;
			wl_list_remove(wl_resource_get_link(r));
			wl_list_insert(&mouse.active, wl_resource_get_link(r));
			wl_pointer_send_enter(r, serial, s, x << 8, y << 8);
		}
	}
	mouse.focus = w;
}

static void
kbdfocus(struct window *w)
{
	static const struct wl_array nokeys;
	struct wl_client *c;
	struct wl_resource *r, *s, *tmp;
	uint32_t serial;

	if (kbd.focus == w)
		return;
	serial = wl_display_next_serial(dpy);
	if (kbd.focus) {
		s = kbd.focus->surface->resource;
		wl_resource_for_each(r, &kbd.active)
			wl_keyboard_send_leave(r, serial, s);
		wl_list_insert_list(&kbd.inactive, &kbd.active);
		wl_list_init(&kbd.active);
	}
	if (w) {
		c = wl_resource_get_client(w->xdgsurface);
		s = w->surface->resource;
		wl_resource_for_each_safe(r, tmp, &kbd.inactive) {
			if (wl_resource_get_client(r) != c)
				continue;
			wl_list_remove(wl_resource_get_link(r));
			wl_list_insert(&kbd.active, wl_resource_get_link(r));
			wl_keyboard_send_enter(r, serial, s, (struct wl_array *)&nokeys);
		}
	}
	kbd.focus = w;
}

static void
wctlread(C9r *reply, void *data)
{
	static struct wl_array states;
	struct window *w;
	char *pos, *current, *hidden, buf[sizeof w->name + 6];
	int x0, y0, x1, y1, needconfig;
	size_t namelen;
	C9r *r;

	w = data;
	w->wctltag = -1;
	if (reply->type == Rerror) {
		xdg_toplevel_send_close(w->toplevel);
		return;
	}
	if (fsread(&termctx, &w->wctltag, NULL, w->wctl, 0, 72) != 0)
		return;
	fsasync(&termctx, w->wctltag, wctlread, w);
	if (reply->read.size != 72) {
		fprintf(stderr, "unexpected wctl read size: %"PRIu32" != 72\n", reply->read.size);
		return;
	}
	pos = (char *)reply->read.data;
	pos[71] = '\0';
	x0 = strtol(pos, &pos, 10) + BORDER;
	y0 = strtol(pos, &pos, 10) + BORDER;
	x1 = strtol(pos, &pos, 10) - BORDER;
	y1 = strtol(pos, &pos, 10) - BORDER;
	current = strtok_r(pos, " ", &pos);
	hidden = strtok_r(NULL, " ", &pos);
	needconfig = 0;
	if (x0 != w->x0 || y0 != w->y0 || x1 != w->x1 || y1 != w->y1) {
		/* resized or moved */
		if (x1 - x0 != w->x1 - w->x0 || y1 - y0 != w->y1 - w->y0)
			needconfig = 1;
		w->x0 = x0, w->y0 = y0;
		w->x1 = x1, w->y1 = y1;
		if (fsread(&termctx, NULL, &r, w->winname, 0, 31) != 0 || r->read.size > 31)
			return;
		namelen = r->read.size;
		memcpy(w->name, r->read.data, namelen);
		w->name[namelen] = '\0';
		free(r);

		pos = buf;
		if (w->image >= 0) {
			*pos++ = 'f';
			pos = putle32(pos, w->image);
		} else {
			w->image = numget(&draw.imgid);
			if (w->image < 0)
				return;
		}
		*pos++ = 'n';
		pos = putle32(pos, w->image);
		*pos++ = namelen;
		memcpy(pos, w->name, namelen), pos += namelen;
		assert(pos - buf < sizeof buf);
		if (fswrite(&termctx, NULL, draw.datafid, 0, buf, pos - buf) != 0) {
			fprintf(stderr, "fswrite %s draw: %s\n", w->name, termaux.err);
			return;
		}
		if (!needconfig) {
			struct drawcopy d;
			size_t n;

			d.w = w;
			d.b = wl_shm_buffer_get(w->surface->pending.buffer);
			if (!d.b)
				return;
			d.d.x0 = 0;
			d.d.y0 = 0;
			d.d.x1 = x1 - x0;
			d.d.y1 = y1 - y0;
			d.x = d.d.x0;
			d.y = d.d.y0;
			d.dx = d.d.x1 - d.d.x0;
			n = draw.buflen - 22;
			if (n < 4 * d.dx) {
				d.dx = (4 * d.dx + n - 1) / n;
				d.dy = 1;
			} else {
				d.dy = n / (4 * d.dx);
			}
			drawcopy(&d);
		}
	}
	if (w->current != (strcmp(current, "current") == 0)) {
		needconfig = 1;
		w->current ^= 1;
		if (w->current) {
			kbdfocus(w);
			snarfoffer(w);
		} else {
			if (mouse.focus == w)
				mousefocus(NULL, 0, 0);
			if (kbd.focus == w)
				kbdfocus(NULL);
		}
	}
	if (w->hidden != (strcmp(hidden, "hidden") == 0)) {
		w->hidden ^= 1;
	}
	if (needconfig) {
		states.size = 0;
		addstate(&states, XDG_TOPLEVEL_STATE_MAXIMIZED);
		if (w->current)
			addstate(&states, XDG_TOPLEVEL_STATE_ACTIVATED);
		xdg_toplevel_send_configure(w->toplevel, x1 - x0, y1 - y0, &states);
		w->serial = wl_display_next_serial(dpy);
		xdg_surface_send_configure(w->xdgsurface, w->serial);
	}
}

static void
mouseaxis(struct wl_resource *r, uint32_t time, int val)
{
	uint32_t axis, ver;

	axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
	ver = wl_resource_get_version(r);
	if (ver >= 8)
		wl_pointer_send_axis_value120(r, axis, 120 * val);
	else if (ver >= 5)
		wl_pointer_send_axis_discrete(r, axis, val);
	wl_pointer_send_axis(r, time, axis, 15 * val << 8);
}

static void
mouseread(C9r *reply, void *data)
{
	static const int button[] = {0x110, 0x112, 0x111};
	struct window *w;
	char *pos;
	unsigned long x, y, b, t;
	struct wl_resource *r;
	uint32_t serial, state;
	unsigned long pressed, changed;
	int i;

	w = data;
	w->mousetag = -1;
	if (reply->type == Rerror) {
		fprintf(stderr, "fsread %s mouse: %s\n", w->name, reply->error);
		return;
	}
	if (fsread(&termctx, &w->mousetag, NULL, w->mouse, 0, 49) != 0) {
		fprintf(stderr, "fsread %s mouse: %s\n", w->name, termaux.err);
		return;
	}
	fsasync(&termctx, w->mousetag, mouseread, w);
	if (reply->read.size != 49) {
		fprintf(stderr, "unexpected mouse data: size %"PRIu32" %.*s", reply->read.size, (int)reply->read.size, reply->read.data);
		return;
	}
	pos = (char *)reply->read.data;
	pos[48] = 0;
	if (*pos != 'm')
		return;
	++pos;
	x = strtoul(pos, &pos, 10) - w->x0;
	y = strtoul(pos, &pos, 10) - w->y0;
	b = strtoul(pos, &pos, 10);
	t = strtoul(pos, &pos, 10);
	if (mouse.focus != w) {
		mousefocus(w, x, y);
	} else if (x != w->mousex || y != w->mousey) {
		wl_resource_for_each(r, &mouse.active)
			wl_pointer_send_motion(r, t, x << 8, y << 8);
	}
	pressed = b & ~w->button;
	changed = b ^ w->button;
	wl_resource_for_each(r, &mouse.active) {
		for (i = 0; i < 3; i++) {
			if (~changed & 1ul << i)
				continue;
			serial = wl_display_next_serial(dpy);
			state = pressed & 1ul << i
				? WL_POINTER_BUTTON_STATE_PRESSED
				: WL_POINTER_BUTTON_STATE_RELEASED;
			wl_pointer_send_button(r, serial, t, button[i], state);
		}
		if (pressed & 8)
			mouseaxis(r, t, -1);
		else if (pressed & 16)
			mouseaxis(r, t, +1);
		if (wl_resource_get_version(r) >= 5)
			wl_pointer_send_frame(r);
	}
	w->mousex = x;
	w->mousey = y;
	w->button = b;
}

static unsigned char *
kbdevent(struct window *w, unsigned char *pos, unsigned char *end)
{
	static struct wl_array keys;
	struct wl_resource *r;
	uint32_t *key, *oldkey, state, mods, serial, time, eventkey;
	struct timespec ts;
	size_t n, notfound;
	int needenter;

	end = memchr(pos, '\0', end - pos);
	if (!end) {
		fprintf(stderr, "kbd event is not null terminated\n");
		return NULL;
	}
	switch (*pos) {
	case 'k': state = WL_KEYBOARD_KEY_STATE_PRESSED; break;
	case 'K': state = WL_KEYBOARD_KEY_STATE_RELEASED; break;
	default: return end + 1;
	}
	keys.size = 0;
	for (++pos; pos < end; pos += n) {
		key = wl_array_add(&keys, sizeof *key);
		if (!key) {
			perror(NULL);
			return NULL;
		}
		n = utf8dec(key, pos, end - pos);
		if (n == (size_t)-1) {
			fprintf(stderr, "kbd event contains invalid UTF-8\n");
			return NULL;
		}
	}
	eventkey = 0;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (keys.size < sizeof *key) {
			fprintf(stderr, "kbd press with no keys\n");
			return NULL;
		}
		eventkey = *key;
		keys.size -= sizeof *key;
	}
	mods = 0;
	wl_array_for_each(key, &keys)
		mods |= keymapmod(*key);
	if (kbd.focus != w) {
		kbdfocus(w);
		needenter = 1;
	} else if (keys.size != w->keys.size - !state * sizeof *key) {
		needenter = 1;
	} else {
		notfound = 0;
		wl_array_for_each(oldkey, &w->keys) {
			wl_array_for_each(key, &keys) {
				if (*key == *oldkey)
					goto found;
			}
			++notfound;
			if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
				break;
			eventkey = *oldkey;
		found:;
		}
		needenter = notfound != (state == WL_KEYBOARD_KEY_STATE_RELEASED);
	}
	serial = wl_display_next_serial(dpy);
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		time = ts.tv_sec * 1000ul + ts.tv_sec / 1000000ul;
	else
		time = 0;
	if (needenter) {
		wl_resource_for_each(r, &kbd.active) {
			wl_keyboard_send_enter(r, serial, w->surface->resource, &keys);
			wl_keyboard_send_modifiers(r, serial, mods, 0, 0, 0);
		}
		kbd.focus = w;
	}
	if (!needenter || state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
			mods |= keymapmod(eventkey);
		wl_resource_for_each(r, &kbd.active) {
			wl_keyboard_send_key(r, serial, time, eventkey, state);
			if (mods != kbd.mods)
				wl_keyboard_send_modifiers(r, serial, mods, 0, 0, 0);
		}
	}
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
		keys.size += sizeof *key;
	kbd.mods = mods;
	if (wl_array_copy(&w->keys, &keys) != 0) {
		perror(NULL);
		return NULL;
	}
	return end + 1;
}

static void
kbdread(C9r *reply, void *data)
{
	struct window *w;
	unsigned char *pos, *end;

	w = data;
	w->kbdtag = -1;
	if (reply->type == Rerror) {
		fprintf(stderr, "fsread %s kbd: %s\n", w->name, reply->error);
		return;
	}
	if (fsread(&termctx, &w->kbdtag, NULL, w->kbd, 0, 256) != 0) {
		fprintf(stderr, "fsread %s kbd: %s\n", w->name, reply->error);
		return;
	}
	fsasync(&termctx, w->kbdtag, kbdread, w);
	pos = reply->read.data;
	end = pos + reply->read.size;
	while (pos && pos < end)
		pos = kbdevent(w, pos, end);
}

static int
winnew(struct window *w)
{
	struct {
		const char *name;
		int *fid;
		C9mode mode;
		void (*readcb)(C9r *, void *);
		int readsz;
		C9tag tag;
		int clunk, flush;
	} *f, files[] = {
		{"winname", &w->winname, C9read},
		{"label",   &w->label,   C9write},
		{"wctl",    &w->wctl,    C9rdwr, NULL,      72},
		{"mouse",   &w->mouse,   C9read, mouseread, 49},
		{"kbd",     &w->kbd,     C9read, kbdread,   256},
	};
	char aname[32];
	C9r *r;

	if (wl_resource_get_client(w->xdgsurface) == child) {
		w->wsys = fswalk(&termctx, NULL, term.root, (const char *[]){"dev", 0});
		if (w->wsys < 0) {
			fprintf(stderr, "fswalk /dev: %s\n", termaux.err);
			return -1;
		}
		child = NULL;
	} else {
		/* XXX: depends on exportfs patch */
		snprintf(aname, sizeof aname, "%11d new", term.wsys);
		w->wsys = fsattach(&termctx, aname);
		if (w->wsys < 0) {
			fprintf(stderr, "fsattach wsys: %s\n", termaux.err);
			return -1;
		}
	}
	w->image = -1;

	for (f = files; f < files + LEN(files); f++) {
		f->clunk = 0;
		*f->fid = fswalk(&termctx, &f->tag, w->wsys, (const char *[]){f->name, 0});
		if (*f->fid < 0) {
			fprintf(stderr, "fswalk %s: %s\n", f->name, termaux.err);
			goto error;
		}
	}
	for (f = files; f < files + LEN(files); f++) {
		r = fswait(&termctx, f->tag, Rwalk);
		if (!r) {
			fprintf(stderr, "walk %s: %s\n", f->name, termaux.err);
			goto error;
		}
		free(r);
		f->clunk = 1;
		if (fsopen(&termctx, &f->tag, *f->fid, f->mode) != 0) {
			fprintf(stderr, "open %s: %s\n", f->name, termaux.err);
			goto error;
		}
	}
	for (f = files; f < files + LEN(files); f++) {
		r = fswait(&termctx, f->tag, Ropen);
		if (!r) {
			fprintf(stderr, "open %s: %s\n", f->name, termaux.err);
			goto error;
		}
		f->flush = 0;
		if (r->type == Rerror) {
			free(r);
			goto error;
		}
		free(r);
		if (f->readsz == 0)
			continue;
		if (fsread(&termctx, &f->tag, NULL, *f->fid, 0, f->readsz) != 0) {
			fprintf(stderr, "read %s: %s\n", f->name, termaux.err);
			goto error;
		}
		if (f->readcb)
			fsasync(&termctx, f->tag, f->readcb, w);
	}
	r = fswait(&termctx, files[2].tag, Rread);
	if (!r)
		goto error;
	wctlread(r, w);
	w->mousetag = files[3].tag;
	w->kbdtag = files[4].tag;
	return 0;

error:
	fsclunk(&termctx, w->wsys);
	w->wsys = -1;
	for (f = files; f < files + LEN(files); f++) {
		numput(&termaux.fid, *f->fid);
		if (f->flush)
			fsflush(&termctx, f->tag);
		if (f->clunk)
			fsclunk(&termctx, *f->fid);
		*f->fid = -1;
	}
	return -1;
}

/* generic resource destructor */
static void
destroy(struct wl_client *c, struct wl_resource *r)
{
	wl_resource_destroy(r);
}

/* wl_surface */
static void
attach(struct wl_client *c, struct wl_resource *r, struct wl_resource *b, int32_t x, int32_t y)
{
	struct surface *s;

	s = wl_resource_get_user_data(r);
	if (s->pending.buffer)
		wl_list_remove(&s->pending.buffer_destroy.link);
	s->pending.buffer = b;
	if (b)
		wl_resource_add_destroy_listener(b, &s->pending.buffer_destroy);
}

static void
damage(struct wl_client *c, struct wl_resource *r, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
	struct surface *s;
	struct damage *d;
	int x1, y1;

	x1 = x0 + w;
	y1 = y0 + h;
	s = wl_resource_get_user_data(r);
	d = &s->pending.damage;
	/* TODO: better damage tracking */
	if (d->x0 == -1 || x0 < d->x0)
		d->x0 = x0;
	if (d->y0 == -1 || y0 < d->y0)
		d->y0 = y0;
	if (d->x1 == -1 || x1 > d->x1)
		d->x1 = x1;
	if (d->y1 == -1 || y1 > d->y1)
		d->y1 = y1;
}

static void
unlink_resource(struct wl_resource *r)
{
	wl_list_remove(wl_resource_get_link(r));
}

static void
frame(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct surface *s;
	struct wl_resource *cbr;

	s = wl_resource_get_user_data(r);
	cbr = wl_resource_create(c, &wl_callback_interface, 1, id);
	if (!cbr) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(cbr, NULL, NULL, unlink_resource);
	wl_list_insert(s->pending.callbacks.prev, wl_resource_get_link(cbr));
}

static void
set_opaque_region(struct wl_client *c, struct wl_resource *r, struct wl_resource *reg)
{
}

static void
set_input_region(struct wl_client *c, struct wl_resource *r, struct wl_resource *reg)
{
}

static void
commit(struct wl_client *c, struct wl_resource *r)
{
	struct surface *s;

	s = wl_resource_get_user_data(r);
	wl_list_insert_list(&s->state.callbacks, &s->pending.callbacks);
	wl_list_init(&s->pending.callbacks);
	if (s->commit)
		s->commit(s);
	if (s->state.buffer != s->pending.buffer) {
		if (s->state.buffer) {
			wl_buffer_send_release(s->state.buffer);
			wl_list_remove(&s->state.buffer_destroy.link);
		}
		if (s->pending.buffer)
			wl_resource_add_destroy_listener(s->pending.buffer, &s->state.buffer_destroy);
		s->state.buffer = s->pending.buffer;
	}
}

static void
set_buffer_transform(struct wl_client *c, struct wl_resource *r, int32_t mode)
{
}

static void
set_buffer_scale(struct wl_client *c, struct wl_resource *r, int32_t scale)
{
	if (scale != 1)
		wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SCALE, "set_buffer_scale is not supported");
}

static void
offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y)
{
}

static const struct wl_surface_interface surface_impl = {
	.destroy = destroy,
	.attach = attach,
	.damage = damage,
	.frame = frame,
	.set_opaque_region = set_opaque_region,
	.set_input_region = set_input_region,
	.commit = commit,
	.set_buffer_transform = set_buffer_transform,
	.set_buffer_scale = set_buffer_scale,
	.damage_buffer = damage,
	.offset = offset,
};

static void
surface_buffer_destroyed(struct wl_listener *listener, void *data)
{
	struct surface_state *state;

	state = wl_container_of(listener, state, buffer_destroy);
	state->buffer = NULL;
}

static void
destroy_surface(struct wl_resource *r)
{
	struct surface *s;

	s = wl_resource_get_user_data(r);
	if (s->state.buffer)
		wl_list_remove(&s->state.buffer_destroy.link);
	if (s->pending.buffer)
		wl_list_remove(&s->pending.buffer_destroy.link);
	free(s);
}

/* wl_region */
static void
region_add(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
}

static const struct wl_region_interface region_impl = {
	.destroy = destroy,
	.add = region_add,
};

/* wl_compositor */
static void
create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct surface *s;
	uint32_t ver;

	s = calloc(1, sizeof *s);
	if (!s)
		goto error;
	ver = wl_resource_get_version(r);
	s->resource = wl_resource_create(c, &wl_surface_interface, ver, id);
	if (!s->resource)
		goto error;
	s->pending.buffer_destroy.notify = surface_buffer_destroyed;
	s->pending.damage.x0 = -1;
	s->pending.damage.y0 = -1;
	s->pending.damage.x1 = -1;
	s->pending.damage.y1 = -1;
	s->state.buffer_destroy.notify = surface_buffer_destroyed;
	s->state.damage.x0 = -1;
	s->state.damage.y0 = -1;
	s->state.damage.x1 = -1;
	s->state.damage.y1 = -1;
	wl_list_init(&s->pending.callbacks);
	wl_list_init(&s->state.callbacks);
	wl_resource_set_implementation(s->resource, &surface_impl, s, destroy_surface);
	return;

error:
	free(s);
	wl_client_post_no_memory(c);
}

static void
create_region(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *nr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	nr = wl_resource_create(c, &wl_region_interface, ver, id);
	if (!nr) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(nr, &region_impl, nr, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
	.create_surface = create_surface,
	.create_region = create_region,
};

static void
bind_compositor(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &wl_compositor_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &compositor_impl, NULL, NULL);
}

/* xdg_toplevel */
static void
set_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *p)
{
}

static void
set_title(struct wl_client *c, struct wl_resource *r, const char *title)
{
	struct window *w;

	w = wl_resource_get_user_data(r);
	if (w->label == -1)
		return;
	fswrite(&termctx, NULL, w->label, 0, title, strlen(title));
}

static void
set_app_id(struct wl_client *c, struct wl_resource *r, const char *appid)
{
}

static void
show_window_menu(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
}

static void
move(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial)
{
}

static void
resize(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
}

static void
set_max_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h)
{
}

static void
set_min_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h)
{
}

static void
set_maximized(struct wl_client *c, struct wl_resource *r)
{
}

static void
unset_maximized(struct wl_client *c, struct wl_resource *r)
{
}

static void
set_fullscreen(struct wl_client *c, struct wl_resource *r, struct wl_resource *output)
{
}

static void
unset_fullscreen(struct wl_client *c, struct wl_resource *r)
{
}

static void
set_minimized(struct wl_client *c, struct wl_resource *r)
{
}

static const struct xdg_toplevel_interface toplevel_impl = {
	.destroy = destroy,
	.set_parent = set_parent,
	.set_title = set_title,
	.set_app_id = set_app_id,
	.show_window_menu = show_window_menu,
	.move = move,
	.resize = resize,
	.set_max_size = set_max_size,
	.set_min_size = set_min_size,
	.set_maximized = set_maximized,
	.unset_maximized = unset_maximized,
	.set_fullscreen = set_fullscreen,
	.unset_fullscreen = unset_fullscreen,
	.set_minimized = set_minimized,
};

static void
toplevel_commit(struct surface *s)
{
	struct window *w;
	struct drawcopy d;
	struct wl_resource *r;
	struct wl_client *c;
	size_t n;

	w = wl_resource_get_user_data(s->role);
	if (w->initial_commit) {
		c = wl_resource_get_client(w->xdgsurface);
		r = wl_resource_find_for_client(&output.resources, c);
		if (r)
			wl_surface_send_enter(s->resource, r);

		winnew(w);
		w->initial_commit = 0;
	}

	d.w = w;
	d.b = wl_shm_buffer_get(s->pending.buffer);
	if (!d.b)
		return;
	d.d = s->pending.damage;
	if (d.d.x0 == -1)
		return;
	if (d.d.x1 > w->x1 - w->x0)
		d.d.x1 = w->x1 - w->x0;
	if (d.d.y1 > w->y1 - w->y0)
		d.d.y1 = w->y1 - w->y0;
	d.x = d.d.x0;
	d.y = d.d.y0;
	d.dx = d.d.x1 - d.d.x0;
	n = draw.buflen - 22;
	if (n < 4 * d.dx) {
		d.dx = (4 * d.dx + n - 1) / n;
		d.dy = 1;
	} else {
		d.dy = n / (4 * d.dx);
	}
	drawcopy(&d);
}

static void
toplevel_destroy(struct wl_resource *r)
{
	struct window *w;
	char buf[5];

	w = wl_resource_get_user_data(r);
	if (w->wsys != -1) {
		fsclunk(&termctx, w->wsys);
		fsclunk(&termctx, w->wctl);
		if (w->wctltag != -1)
			fsflush(&termctx, w->wctltag);
		fsclunk(&termctx, w->winname);
		fsclunk(&termctx, w->label);
		fsclunk(&termctx, w->mouse);
		if (w->mousetag != -1)
			fsflush(&termctx, w->mousetag);
		fsclunk(&termctx, w->kbd);
		if (w->kbdtag != -1)
			fsflush(&termctx, w->kbdtag);
	}
	if (w->image != -1) {
		buf[0] = 'f';
		putle32(buf + 1, w->image);
		if (fswrite(&termctx, NULL, draw.datafid, 0, buf, sizeof buf) != 0)
			fprintf(stderr, "fswrite %s draw: %s\n", w->name, strerror(errno));
	}
	w->toplevel = NULL;
	w->surface->role = NULL;
	w->surface->commit = NULL;
}

/* xdg_popup */
static void
grab(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial)
{
}

static void
reposition(struct wl_client *c, struct wl_resource *r, struct wl_resource *pos, uint32_t token)
{
}

static const struct xdg_popup_interface popup_impl = {
	.destroy = destroy,
	.grab = grab,
	.reposition = reposition,
};

/* wl_data_offer */
static void
offer_accept(struct wl_client *c, struct wl_resource *r, uint32_t serial, const char *mime)
{
	struct wl_resource *ddr;

	if (!mime || strcmp(mime, "text/plain;charset=utf-8") != 0)
		return;
	return;
	ddr = wl_resource_find_for_client(&datadev.resources, c);
	if (ddr)
		wl_data_device_send_selection(ddr, r);
}

static void
offer_receive(struct wl_client *c, struct wl_resource *r, const char *mime, int32_t fd)
{
	struct snarfget *get;
	C9tag tag;

	get = malloc(sizeof *get);
	if (!get) {
		perror(NULL);
		close(fd);
		return;
	}
	get->fd = fd;
	get->offset = 0;
	get->offer = r;
	if (fsread(&termctx, &tag, NULL, term.snarf, 0, 1024) != 0) {
		close(fd);
		free(get);
		return;
	}
	fsasync(&termctx, tag, snarfget, get);
}

static void
offer_finish(struct wl_client *c, struct wl_resource *r)
{
}

static void
offer_set_actions(struct wl_client *c, struct wl_resource *r, uint32_t actions, uint32_t preferred)
{
}

static const struct wl_data_offer_interface offer_impl = {
	.accept = offer_accept,
	.receive = offer_receive,
	.destroy = destroy,
	.finish = offer_finish,
	.set_actions = offer_set_actions,
};

/* xdg_surface */
static void
get_toplevel(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct window *w;
	uint32_t ver;

	w = wl_resource_get_user_data(r);
	ver = wl_resource_get_version(r);
	w->toplevel = wl_resource_create(c, &xdg_toplevel_interface, ver, id);
	if (!w->toplevel) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(w->toplevel, &toplevel_impl, w, toplevel_destroy);
	w->surface->role = w->toplevel;
	w->surface->commit = toplevel_commit;
}

static void
get_popup(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *parentr, struct wl_resource *posr)
{
	struct wl_resource *pr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	pr = wl_resource_create(c, &xdg_popup_interface, ver, id);
	if (!pr) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(pr, &popup_impl, NULL, NULL);
}

static void
set_window_geometry(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
}

static void
ack_configure(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{
}

static const struct xdg_surface_interface xdg_surface_impl = {
	.destroy = destroy,
	.get_toplevel = get_toplevel,
	.get_popup = get_popup,
	.set_window_geometry = set_window_geometry,
	.ack_configure = ack_configure,
};

static void
xdgsurface_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct window *w;

	w = wl_container_of(listener, w, surface_destroy);
	wl_resource_destroy(w->xdgsurface);
}

static void
xdg_surface_destroy(struct wl_resource *r)
{
	struct window *w;

	w = wl_resource_get_user_data(r);
	wl_list_remove(&w->surface_destroy.link);
	if (w->surface->role)
		wl_resource_destroy(w->surface->role);
	if (kbd.focus == w)
		kbdfocus(NULL);
	if (mouse.focus == w)
		mousefocus(NULL, 0, 0);
	free(w);
}

/* xdg_positioner */
static void
set_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h)
{
}

static void
set_anchor_rect(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
}

static void
set_anchor(struct wl_client *c, struct wl_resource *r, uint32_t anchor)
{
}

static void
set_gravity(struct wl_client *c, struct wl_resource *r, uint32_t gravity)
{
}

static void
set_constraint_adjustment(struct wl_client *c, struct wl_resource *r, uint32_t adj)
{
}

static void
set_offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y)
{
}

static void
set_reactive(struct wl_client *c, struct wl_resource *r)
{
}

static void
set_parent_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h)
{
}

static void
set_parent_configure(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{
}

static const struct xdg_positioner_interface positioner_impl = {
	.destroy = destroy,
	.set_size = set_size,
	.set_anchor_rect = set_anchor_rect,
	.set_anchor = set_anchor,
	.set_gravity = set_gravity,
	.set_constraint_adjustment = set_constraint_adjustment,
	.set_offset = set_offset,
	.set_reactive = set_reactive,
	.set_parent_size = set_parent_size,
	.set_parent_configure = set_parent_configure,
};

/* xdg_wm_base */
static void
create_positioner(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *pr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	pr = wl_resource_create(c, &xdg_positioner_interface, ver, id);
	if (!pr) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(pr, &positioner_impl, NULL, NULL);
}

static void
get_xdg_surface(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *sr)
{
	struct surface *s;
	struct window *w;
	uint32_t ver;

	s = wl_resource_get_user_data(sr);
	w = calloc(1, sizeof *w);
	if (!w)
		goto error;
	ver = wl_resource_get_version(r);
	w->xdgsurface = wl_resource_create(c, &xdg_surface_interface, ver, id);
	if (!w->xdgsurface)
		goto error;
	wl_array_init(&w->keys);
	w->surface = s;
	w->initial_commit = 1;
	w->surface_destroy.notify = xdgsurface_surface_destroyed;
	wl_resource_add_destroy_listener(s->resource, &w->surface_destroy);
	w->wsys = -1;
	w->wctl = -1;
	w->wctltag = -1;
	w->winname = -1;
	w->label = -1;
	w->mouse = -1;
	w->mousetag = -1;
	w->kbd = -1;
	w->kbdtag = -1;
	wl_resource_set_implementation(w->xdgsurface, &xdg_surface_impl, w, xdg_surface_destroy);
	return;

error:
	free(w);
	wl_client_post_no_memory(c);
}

static void
pong(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{
}

static const struct xdg_wm_base_interface wm_impl = {
	.destroy = destroy,
	.create_positioner = create_positioner,
	.get_xdg_surface = get_xdg_surface,
	.pong = pong,
};

static void
bind_wm(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &xdg_wm_base_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &wm_impl, NULL, NULL);
}

/* wl_pointer */
static void
set_cursor(struct wl_client *c, struct wl_resource *r, uint32_t serial, struct wl_resource *sr, int32_t x, int32_t y)
{
	/* TODO: implement */
}

static const struct wl_pointer_interface pointer_impl = {
	.set_cursor = set_cursor,
	.release = destroy,
};

/* wl_keyboard */
static const struct wl_keyboard_interface keyboard_impl = {
	.release = destroy,
};

/* wl_seat */
static void
get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *nr;
	struct window *w;
	uint32_t ver;
	int active;

	ver = wl_resource_get_version(r);
	nr = wl_resource_create(c, &wl_pointer_interface, ver, id);
	if (!nr) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(nr, &pointer_impl, NULL, unlink_resource);
	w = mouse.focus;
	active = w && wl_resource_get_client(r) == wl_resource_get_client(w->xdgsurface);
	wl_list_insert(active ? &mouse.active : &mouse.inactive, wl_resource_get_link(nr));
}

static void
get_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *nr;
	struct window *w;
	uint32_t ver;
	int active;

	ver = wl_resource_get_version(r);
	nr = wl_resource_create(c, &wl_keyboard_interface, ver, id);
	if (!nr) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(nr, &keyboard_impl, NULL, unlink_resource);
	wl_keyboard_send_keymap(nr, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, kbd.keymapfd, kbd.keymapsize);
	w = kbd.focus;
	active = w && wl_resource_get_client(r) == wl_resource_get_client(w->xdgsurface);
	wl_list_insert(active ? &kbd.active : &kbd.inactive, wl_resource_get_link(nr));
}

static void
get_touch(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	wl_resource_post_error(r, WL_SEAT_ERROR_MISSING_CAPABILITY, "no touch");
}

static const struct wl_seat_interface seat_impl = {
	.get_pointer = get_pointer,
	.get_keyboard = get_keyboard,
	.get_touch = get_touch,
	.release = destroy,
};

static void
bind_seat(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &wl_seat_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &seat_impl, NULL, NULL);

	wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
}

/* wl_data_source */
static void
offer(struct wl_client *c, struct wl_resource *r, const char *mime)
{
}

static void
set_actions(struct wl_client *c, struct wl_resource *r, uint32_t actions)
{
}

static const struct wl_data_source_interface data_source_impl = {
	.offer = offer,
	.destroy = destroy,
	.set_actions = set_actions,
};

/* wl_data_device */
static void
create_data_source(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *nr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	nr = wl_resource_create(c, &wl_data_source_interface, ver, id);
	if (!nr) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(nr, &data_source_impl, NULL, NULL);
}

static void
start_drag(struct wl_client *c, struct wl_resource *r, struct wl_resource *source, struct wl_resource *origin, struct wl_resource *icon, uint32_t serial)
{
}

static void
set_selection(struct wl_client *c, struct wl_resource *r, struct wl_resource *source, uint32_t serial)
{
	struct snarfput *put;
	int fd[2];

	put = malloc(sizeof *put);
	if (!put) {
		perror(NULL);
		return;
	}
	if (pipe(fd) != 0) {
		perror("pipe");
		free(put);
		return;
	}
	wl_data_source_send_send(source, "text/plain;charset=utf-8", fd[1]);
	close(fd[1]);
	put->offset = 0;
	put->source = source;
	put->snarf = fswalk(&termctx, NULL, term.root, (const char *[]){"dev", "snarf", 0});
	if (put->snarf < 0) {
		close(fd[0]);
		free(put);
		return;
	}
	if (fsopen(&termctx, NULL, put->snarf, C9write) != 0) {
		fsclunk(&termctx, put->snarf);
		close(fd[0]);
		free(put);
		return;
	}
	put->event = wl_event_loop_add_fd(evt, fd[0], WL_EVENT_READABLE, snarfput, put);
}

static const struct wl_data_device_interface data_device_impl = {
	.start_drag = start_drag,
	.set_selection = set_selection,
	.release = destroy,
};

/* wl_data_device_manager */
static void
get_data_device(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *seat)
{
	struct wl_resource *nr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	nr = wl_resource_create(c, &wl_data_device_interface, ver, id);
	if (!nr) {
		wl_resource_post_no_memory(r);
		return;
	}
	wl_resource_set_implementation(nr, &data_device_impl, NULL, unlink_resource);
	wl_list_insert(&datadev.resources, wl_resource_get_link(nr));
}

static const struct wl_data_device_manager_interface dataman_impl = {
	.create_data_source = create_data_source,
	.get_data_device = get_data_device,
};

static void
bind_dataman(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &wl_data_device_manager_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &dataman_impl, NULL, NULL);
}

/* wl_output */
static void
bind_output(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &wl_output_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, NULL, NULL, unlink_resource);
	wl_list_insert(&output.resources, wl_resource_get_link(r));
	wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "plan9", "rio", WL_OUTPUT_TRANSFORM_NORMAL);
	wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT, draw.x1 - draw.x0, draw.y1 - draw.y0, 0);
	wl_output_send_done(r);
}

static void
request_mode(struct wl_client *c, struct wl_resource *r, uint32_t mode)
{
}

/* org_kde_kwin_server_decoration */
static const struct org_kde_kwin_server_decoration_interface deco_impl = {
	.release = destroy,
	.request_mode = request_mode,
};

static void
decoration_create(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface)
{
	struct wl_resource *dr;
	uint32_t ver;

	ver = wl_resource_get_version(r);
	dr = wl_resource_create(c, &org_kde_kwin_server_decoration_interface, ver, id);
	if (!dr) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(dr, &deco_impl, NULL, NULL);
	org_kde_kwin_server_decoration_send_mode(dr, ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
}

/* org_kde_kwin_server_decoration_manager */
static const struct org_kde_kwin_server_decoration_manager_interface decoman_impl = {
	.create = decoration_create,
};

static void
bind_decoman(struct wl_client *c, void *p, uint32_t ver, uint32_t id)
{
	struct wl_resource *r;

	r = wl_resource_create(c, &org_kde_kwin_server_decoration_manager_interface, ver, id);
	if (!r) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(r, &decoman_impl, NULL, NULL);
	org_kde_kwin_server_decoration_manager_send_default_mode(r, ORG_KDE_KWIN_SERVER_DECORATION_MODE_SERVER);
}

static int
drawinit(C9ctx *ctx)
{
	C9aux *aux;
	C9r *r;
	C9tag tag;
	char conn[12];

	aux = ctx->aux;
	if (numget(&draw.imgid) != 0) {
		perror(NULL);
		return -1;
	}

	draw.buflen = 32768;
	if (draw.datafd < 0) {
		draw.ctlfid = fswalk(ctx, NULL, draw.datafid, (const char *[]){"dev", "draw", "new", 0});
		if (draw.ctlfid < 0) {
			fprintf(stderr, "fswalk /dev/draw/new: %s\n", aux->err);
			return -1;
		}
		if (fsopen(ctx, NULL, draw.ctlfid, C9read) != 0) {
			fprintf(stderr, "fsopen /dev/draw/new: %s\n", aux->err);
			return -1;
		}
		if (fsread(ctx, NULL, &r, draw.ctlfid, 0, 144) != 0) {
			fprintf(stderr, "fsread /dev/draw/new: %s\n", aux->err);
			return -1;
		}
		if (r->read.size < 96) {
			fprintf(stderr, "fsread /dev/draw/new: too short\n");
			return -1;
		}
		r->read.data[11] = '\0';
		strcpy(conn, (char *)r->read.data + strspn((char *)r->read.data, " "));
		r->read.data[96] = '\0';
		draw.x0 = atoi((char *)r->read.data + 48);
		draw.y0 = atoi((char *)r->read.data + 60);
		draw.x1 = atoi((char *)r->read.data + 72);
		draw.y1 = atoi((char *)r->read.data + 84);
		free(r);
		draw.datafid = fswalk(ctx, NULL, term.root, (const char *[]){"dev", "draw", conn, "data", 0});
		if (draw.datafid < 0) {
			fprintf(stderr, "fswalk /dev/draw/%s/data: %s\n", conn, aux->err);
			return -1;
		}
		if (fsopen(ctx, &tag, draw.datafid, C9write) != 0) {
			fprintf(stderr, "fsopen /dev/draw/%s/data: %s\n", conn, aux->err);
			return -1;
		}
		r = fswait(ctx, tag, Ropen);
		if (!r) {
			fprintf(stderr, "fsopen /dev/draw/%s/data: %s\n", conn, aux->err);
			return -1;
		}
		if (r->iounit)
			draw.buflen = r->iounit;
		free(r);
	}
	draw.buf = malloc(draw.buflen);
	if (!draw.buf) {
		perror(NULL);
		return -1;
	}

	return 0;
}

static int
kbmapline(void *aux, char **str, size_t *len)
{
	static uint64_t off;
	C9r *r;
	int fid;

	fid = *(int *)aux;
	if (fsread(&termctx, NULL, &r, fid, off, 36) != 0) {
		fprintf(stderr, "read /dev/kbmap: %s\n", termaux.err);
		return -1;
	}
	if (r->read.size == 0)
		return 0;
	if (r->read.size != 36 || r->read.data[r->read.size - 1] != '\n') {
		fprintf(stderr, "read /dev/kbmap: unexpected line size\n");
		return -1;
	}
	off += r->read.size;
	r->read.data[r->read.size - 1] = '\0';
	*str = (char *)r->read.data;
	*len = r->read.size - 1;
	return 1;
}

static int
keymapinit(C9ctx *ctx)
{
	C9aux *aux;
	FILE *f;
	int fid;
	struct stat st;

	aux = ctx->aux;
	f = tmpfile();
	if (!f) {
		perror("tmpfile");
		return -1;
	}
	fid = fswalk(ctx, NULL, term.root, (const char *[]){"dev", "kbmap", 0});
	if (fid < 0) {
		fprintf(stderr, "fswalk /dev/kbmap: %s\n", aux->err);
		return -1;
	}
	if (fsopen(ctx, NULL, fid, C9read) != 0) {
		fprintf(stderr, "fsopen /dev/kbmap: %s\n", aux->err);
		return -1;
	}
	if (writekeymap(f, kbmapline, &fid) != 0)
		return -1;
	if (fsclunk(ctx, fid) != 0)
		return -1;
	kbd.keymapfd = dup(fileno(f));
	if (kbd.keymapfd < 0) {
		perror("dup");
		return -1;
	}
	fclose(f);
	if (fstat(kbd.keymapfd, &st) != 0) {
		perror("fstat");
		return -1;
	}
	kbd.keymapsize = st.st_size;
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: wl9 [-t termrfd[,termwfd]] [-w wsysrfd[,wsyswfd]] [-d datawfd]\n");
	exit(1);
}

static void
fdpair(char *s, int *rfd, int *wfd)
{
	long fd;

	errno = 0;
	fd = strtol(s, &s, 10);
	if (errno != 0 || fd < 0 || fd > INT_MAX)
		usage();
	*rfd = fd;
	if (wfd && *s == ',') {
		errno = 0;
		fd = strtol(s + 1, &s, 10);
		if (errno != 0 || fd < 0 || fd > INT_MAX || *s != '\0')
			usage();
	} else if (*s != '\0') {
		usage();
	}
	if (wfd)
		*wfd = fd;
}

char *
termenv(const char *var)
{
	C9r *r;
	int fid;
	char *val;

	fid = fswalk(&termctx, NULL, term.root, (const char*[]){"env", var, 0});
	if (fid < 0) {
		fprintf(stderr, "fswalk /env/%s: %s\n", var, termaux.err);
		return NULL;
	}
	if (fsopen(&termctx, NULL, fid, C9read) != 0) {
		fprintf(stderr, "fsopen /env/%s: %s\n", var, termaux.err);
		fsclunk(&termctx, fid);
		return NULL;
	}
	if (fsread(&termctx, NULL, &r, fid, 0, 128) != 0) {
		fprintf(stderr, "fsread /env/%s: %s\n", var, termaux.err);
		fsclunk(&termctx, fid);
		return NULL;
	}
	val = malloc(r->read.size + 1);
	if (!val) {
		perror(NULL);
		return NULL;
	}
	memcpy(val, r->read.data, r->read.size);
	val[r->read.size] = '\0';
	return val;
}

static char *
splitpath(char *path, const char *wname[], size_t wnamemax)
{
	size_t nwname;

	if (path[0] != '/')
		return "not an absolute path";
	nwname = 0;
	for (; *path; ++path) {
		if (nwname + 1 >= wnamemax)
			return "too many components";
		if (*path == '/') {
			wname[nwname++] = path + 1;
			*path = '\0';
		}
	}
	wname[nwname] = NULL;
	return NULL;
}

static void
child_destroyed(struct wl_listener *l, void *data)
{
	child = NULL;
}

static void
launch(char *argv[])
{
	static struct wl_listener child_destroy;
	extern char **environ;
	int fd[2], err;
	char fdstr[(sizeof fd[0] * CHAR_BIT + 2) / 3 + 1];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) != 0) {
		perror("socketpair");
		exit(1);
	}
	if (fcntl(fd[0], F_SETFD, FD_CLOEXEC) != 0) {
		perror("fcntl FD_CLOEXEC");
		exit(1);
	}
	child = wl_client_create(dpy, fd[0]);
	if (!child) {
		perror("wl_client_create");
		exit(1);
	}
	child_destroy.notify = child_destroyed;
	wl_client_add_destroy_listener(child, &child_destroy);
	snprintf(fdstr, sizeof fdstr, "%d", fd[1]);
	setenv("WAYLAND_SOCKET", fdstr, 1);
	err = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
	if (err) {
		fprintf(stderr, "spawn %s: %s\n", argv[0], strerror(err));
		exit(1);
	}
	close(fd[1]);
}

static int
fsready(int fd, uint32_t mask, void *ptr)
{
	C9ctx *ctx;

	ctx = ptr;
	if (mask & WL_EVENT_WRITABLE)
		fswriteT(ctx);
	if (mask & WL_EVENT_READABLE)
		fsreadR(ctx);
	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct {
		const struct wl_interface *iface;
		int ver;
		wl_global_bind_func_t bind;
	} *g, globals[] = {
		{&wl_compositor_interface, 5, bind_compositor},
		{&wl_seat_interface, 5, bind_seat},
		{&wl_data_device_manager_interface, 3, bind_dataman},
		{&xdg_wm_base_interface, 3, bind_wm},
		{&wl_output_interface, 4, bind_output},
		{&org_kde_kwin_server_decoration_manager_interface, 1, bind_decoman},
	};
	struct wl_list *clients;
	char *wsys, *err;
	const char *wsyspath[C9maxpathel], *sock;
	uint32_t mask;

	termaux.rfd = -1;
	draw.datafd = -1;
	ARGBEGIN {
	case 't':
		fdpair(EARGF(usage()), &termaux.rfd, &termaux.wfd);
		break;
	case 'd':
		fdpair(EARGF(usage()), &draw.datafd, NULL);
		break;
	default:
		usage();
	} ARGEND

	if (termaux.rfd < 0) {
		termaux.rfd = open("/dev/virtio-ports/term", O_RDWR | O_CLOEXEC);
		if (termaux.rfd < 0) {
			perror("open /dev/virtio-ports/term");
			return 1;
		}
		termaux.wfd = termaux.rfd;
	}

	fsinit(&termctx, &termaux);
	fprintf(stderr, "9p msize %"PRIu32"\n", termctx.msize);
	term.root = fsattach(&termctx, NULL);
	if (term.root < 0)
		return 1;

	wsys = termenv("wsys");
	if (!wsys)
		return 1;
	err = splitpath(wsys, wsyspath, LEN(wsyspath));
	if (err) {
		fprintf(stderr, "invalid path '%s': %s\n", wsys, err);
		return 1;
	}
	term.wsys = fswalk(&termctx, NULL, term.root, wsyspath);
	if (term.wsys < 0) {
		fprintf(stderr, "fswalk %s: %s\n", wsys, termaux.err);
		return 1;
	}
	free(wsys);

	term.snarf = fswalk(&termctx, NULL, term.root, (const char *[]){"dev", "snarf", 0});
	if (term.snarf < 0)
		return 1;
	if (fsopen(&termctx, NULL, term.snarf, C9read) != 0)
		return 1;

	if (drawinit(&termctx) != 0)
		return 1;
	if (keymapinit(&termctx) != 0)
		return 1;
	wl_list_init(&mouse.active);
	wl_list_init(&mouse.inactive);
	wl_list_init(&kbd.active);
	wl_list_init(&kbd.inactive);

	dpy = wl_display_create();
	if (!dpy) {
		perror("wl_display_create");
		return 1;
	}
	evt = wl_display_get_event_loop(dpy);
	term.event = wl_event_loop_add_fd(evt, termaux.rfd, WL_EVENT_READABLE, fsready, &termctx);
	if (!term.event) {
		fprintf(stderr, "failed to add 9p event source\n");
		return 1;
	}
	sock = wl_display_add_socket_auto(dpy);
	if (!sock) {
		fprintf(stderr, "failed to add socket\n");
		return 1;
	}
	setenv("WAYLAND_DISPLAY", sock, 1);
	if (wl_display_init_shm(dpy) != 0) {
		fprintf(stderr, "failed to init shm\n");
		return 1;
	}
	for (g = globals; g < globals + LEN(globals); g++) {
		if (!wl_global_create(dpy, g->iface, g->ver, NULL, g->bind)) {
			fprintf(stderr, "wl_global_create %s failed\n", g->iface->name);
			return 1;
		}
	}
	wl_list_init(&output.resources);
	wl_list_init(&datadev.resources);

	if (argc)
		launch(argv);
	clients = wl_display_get_client_list(dpy);
	while (!argc || !wl_list_empty(clients)) {
		wl_display_flush_clients(dpy);
		wl_event_loop_dispatch(evt, -1);
		fsdispatch(&termctx);
		mask = WL_EVENT_READABLE;
		if (termaux.wend > termaux.wpos)
			mask |= WL_EVENT_WRITABLE;
		if (mask != term.eventmask)
			wl_event_source_fd_update(term.event, mask);
	}
}
