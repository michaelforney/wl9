#include "fs.h"

static int ctl;
static int data;
static struct numtab imgid;

static int
drawinit(C9ctx *ctx)
{
	C9aux *aux;
	C9r *r;
	C9tag tag;
	char conn[12];
	int i;

	aux = ctx->aux;
	for (i = 0; i < 3; ++i) {
		if (numget(&draw.imgid) != i) {
			perror(NULL);
			return -1;
		}
	}

	ctlfid = fswalk(ctx, NULL, root, (const char *[]){"dev", "draw", "new", 0});
	if (draw.ctlfid < 0) {
		fprintf(stderr, "fswalk /dev/draw/new: %s\n", aux->err);
		return -1;
	}
	if (fsopen(ctx, NULL, ctl, C9read) != 0) {
		fprintf(stderr, "fsopen /dev/draw/new: %s\n", aux->err);
		return -1;
	}
	if (fsread(ctx, NULL, &r, ctl, 0, 144) != 0) {
		fprintf(stderr, "fsread /dev/draw/new: %s\n", aux->err);
		return -1;
	}
	if (r->read.size < 96) {
		fprintf(stderr, "fsread /dev/draw/new: too short\n");
		return -1;
	}
	p = r->read.data;
	p[11] = '\0';
	strcpy(conn, p + strspn(p, " "));
	p[96] = '\0';
	drawrect.x0 = atoi(p + 48);
	drawrect.y0 = atoi(p + 60);
	drawrect.x1 = atoi(p + 72);
	drawrect.y1 = atoi(p + 84);
	free(r);
	data = fswalk(ctx, NULL, term.root, (const char *[]){"dev", "draw", conn, "data", 0});
	if (data < 0) {
		fprintf(stderr, "fswalk /dev/draw/%s/data: %s\n", conn, aux->err);
		return -1;
	}
	if (fsopen(ctx, &tag, data, C9write) != 0) {
		fprintf(stderr, "fsopen /dev/draw/%s/data: %s\n", conn, aux->err);
		return -1;
	}
	r = fswait(ctx, tag, Ropen);
	if (!r) {
		fprintf(stderr, "fsopen /dev/draw/%s/data: %s\n", conn, aux->err);
		return -1;
	}
	drawbuflen = r->iounit ? r->iounit : 32768;
	free(r);
	drawbuf = malloc(drawbuflen);
	if (!drawbuf) {
		perror(NULL);
		return -1;
	}
	return 0;
}

static int
drawcmd(void *buf, size_t len)
{
	C9aux *aux;

	aux = ctx->aux;
	if (fswrite(ctx, NULL, data, 0, buf, pos - buf) != 0) {
		fprintf(stderr, "fswrite %s draw: %s\n", w->name, aux->err);
		return;
	}
}

static int
draw9pwin(void)
{
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
	if (drawcmd(buf, pos - buf) != 0) {
		fprintf(stderr, "draw failed\n");
		return;
	}
}

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

		if (drawcmd(draw.buf, pos - draw.buf) != 0)
			break;
		/*
		if (fswrite(&termctx, &tag, draw.datafid, 0, draw.buf, pos - draw.buf) != 0) {
			fprintf(stderr, "fswrite %s draw: %s\n", d->w->name, termaux.err);
			break;
		}
		*/
	}
	assert(tag != -1);
	//free(fswait(&termctx, tag, Rwrite));

	d->x = x;
	d->y = y;
}
