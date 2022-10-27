/* SPDX-License-Identifier: ISC */
#define _POSIX_C_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include "c9.h"
#include "util.h"
#include "fs.h"

#define NOTAG 0xffff

struct reply {
	C9r r;
	struct reply *next;
	uint32_t size;
	uint8_t data[];
};

struct callback {
	void (*fn)(C9r *, void *);
	void *aux;
};

static C9error
newtag(C9ctx *ctx, C9ttype type, C9tag *tagp)
{
	C9aux *aux;
	int tag;
	struct callback *cb;

	aux = ctx->aux;
	tag = numget(&ctx->aux->tag);
	if (tag < 0)
		return C9Etag;
	if (tag >= aux->cblen) {
		cb = realloc(aux->cb, (tag + 1) * sizeof *aux->cb);
		if (!cb)
			return C9Etag;
		memset(cb + aux->cblen, 0, (tag + 1 - aux->cblen) * sizeof *aux->cb);
		aux->cb = cb;
		aux->cblen = tag + 1;
	}
	*tagp = tag;
	return 0;
}

static void
freetag(C9ctx *ctx, C9tag tag)
{
	numput(&ctx->aux->tag, tag);
}

static void
write9p(C9ctx *ctx)
{
	C9aux *aux;
	ssize_t ret;
	struct pollfd pfd;

	aux = ctx->aux;
	while (aux->wpos < aux->wend) {
		ret = write(aux->wfd, aux->wpos, aux->wend - aux->wpos);
		if (ret < 0) {
			if (errno == EAGAIN) {
				pfd.fd = aux->wfd;
				pfd.events = POLLOUT;
				poll(&pfd, 1, -1);
				continue;
			}
			snprintf(aux->err, sizeof aux->err, "write: %s", strerror(errno));
			exit(1);
		}
		aux->wpos += ret;
	}
	aux->wpos = aux->wend = aux->wbuf;
}

static uint8_t *
read9p(C9ctx *ctx, uint32_t size, int *err)
{
	C9aux *aux;
	uint8_t *buf;
	ssize_t ret;

	aux = ctx->aux;
	assert(size <= sizeof aux->rbuf);
	if (aux->rend == aux->rpos || aux->rbuf + sizeof aux->rbuf - aux->rpos < size) {
		memmove(aux->rbuf, aux->rpos, aux->rend - aux->rpos);
		aux->rend = aux->rbuf + (aux->rend - aux->rpos);
		aux->rpos = aux->rbuf;
	}
	while (aux->rend - aux->rpos < size) {
		ret = read(aux->rfd, aux->rend, sizeof aux->rbuf - (aux->rend - aux->rbuf));
		if (ret <= 0) {
			*err = ret == 0 || errno != EAGAIN;
			aux->ready = 0;
			return NULL;
		}
		aux->rend += ret;
	}
	buf = aux->rpos;
	aux->rpos += size;
	return buf;
}

static uint8_t *
begin(C9ctx *ctx, uint32_t size)
{
	C9aux *aux;
	uint8_t *buf;

	aux = ctx->aux;
	assert(size <= sizeof aux->wbuf);
	if (size > aux->wbuf + sizeof aux->wbuf - aux->wend)
		write9p(ctx);
	buf = aux->wend;
	aux->wend += size;
	return buf;
}

static int
end(C9ctx *ctx)
{
	return 0;
}

static void
r(C9ctx *ctx, C9r *r)
{
	C9aux *aux;
	struct reply *reply;
	size_t size;

	aux = ctx->aux;
	size = sizeof *reply;
	switch (r->type) {
	case Rerror:
		size = strlen(r->error) + 1;
		reply = malloc(sizeof *reply + size);
		if (!reply)
			goto error;
		reply->r = *r;
		reply->r.error = (char *)reply->data;
		memcpy(reply->data, r->error, size);
		break;
	case Rread:
		size = r->read.size;
		reply = malloc(sizeof *reply + size);
		if (!reply)
			goto error;
		reply->r = *r;
		reply->r.read.data = reply->data;
		memcpy(reply->data, r->read.data, size);
		break;
	default:
		reply = malloc(sizeof *reply);
		if (!reply)
			goto error;
		reply->r = *r;
	}
	reply->next = aux->queue;
	aux->queue = reply;
	return;

error:
	/* XXX: can we handle this better? */
	perror(NULL);
	exit(1);
}

static void
error(C9ctx *ctx, const char *fmt, ...)
{
	C9aux *aux;
	va_list ap;
	int n;

	aux = ctx->aux;
	va_start(ap, fmt);
	n = vsnprintf(aux->err, sizeof aux->err - 1, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof aux->err - 1)
		strcpy(aux->err, "unknown error");
	else
		aux->err[n] = 0;
}

static int
fsversion(C9ctx *ctx, uint32_t msize)
{
	C9aux *aux;
	C9tag tag;
	C9r *r;

	aux = ctx->aux;
	if (c9version(ctx, &tag, msize) != 0)
		goto error;
	r = fswait(ctx, tag, Rversion);
	if (!r)
		goto error;
	free(r);
	return 0;

error:
	fprintf(stderr, "fsversion: %s\n", aux->err);
	return -1;
}

int
fsinit(C9ctx *ctx, C9aux *aux)
{
	ctx->newtag = newtag;
	ctx->freetag = freetag;
	ctx->read = read9p;
	ctx->begin = begin;
	ctx->end = end;
	ctx->r = r;
	ctx->error = error;
	ctx->aux = aux;
	aux->rpos = aux->rend = aux->rbuf;
	aux->wpos = aux->wend = aux->wbuf;
	aux->queue = NULL;
	aux->cb = NULL;
	aux->cblen = 0;
	fcntl(aux->rfd, F_SETFL, O_NONBLOCK);
	return fsversion(ctx, sizeof aux->rbuf);
}

void
fsasync(C9ctx *ctx, C9tag tag, void (*fn)(C9r *, void *), void *data)
{
	C9aux *aux;

	aux = ctx->aux;
	assert(tag < aux->cblen);
	aux->cb[tag].fn = fn;
	aux->cb[tag].aux = data;
}

C9r *
fswait(C9ctx *ctx, C9tag tag, C9rtype type)
{
	C9aux *aux;
	struct reply *r, **rp;
	struct pollfd pfd;

	write9p(ctx);
	aux = ctx->aux;
	for (rp = &aux->queue; (r = *rp); rp = &r->next) {
		if (r->r.tag == tag) {
			*rp = r->next;
			goto found;
		}
	}
	pfd.fd = aux->rfd;
	pfd.events = POLLIN;
	aux->ready = 0;
	for (;;) {
		while (!aux->ready) {
			if (poll(&pfd, 1, -1) != 1) {
				perror("poll");
				exit(1);
			}
			aux->ready = 1;
			if (c9proc(ctx) != 0) {
				fprintf(stderr, "c9proc: %s\n", aux->err);
				exit(1);
			}
		}
		r = aux->queue;
		if (r->r.tag == tag) {
			aux->queue = r->next;
			goto found;
		}
	}
found:
	freetag(ctx, r->r.tag);
	if (r->r.type == Rerror) {
		snprintf(aux->err, sizeof aux->err, "%s", r->r.error);
		free(r);
		r = NULL;
	} else if (r->r.type != type) {
		fprintf(stderr, "fswait: unexpected reply type: %d != %d\n", r->r.type, type);
		exit(1);
	}
	return &r->r;
}

int
fsready(int fd, uint32_t mask, void *data)
{
	C9ctx *ctx;
	C9aux *aux;

	ctx = data;
	aux = ctx->aux;
	aux->ready = 1;
	do {
		if (c9proc(ctx) != 0) {
			fprintf(stderr, "c9proc: %s\n", aux->err);
			exit(1);
		}
	} while (aux->ready);
	return 0;
}

void
fsdispatch(C9ctx *ctx)
{
	C9aux *aux;
	struct reply *r;
	struct callback cb;

	aux = ctx->aux;
	while (aux->queue) {
		r = aux->queue;
		aux->queue = r->next;
		cb.fn = NULL;
		if (r->r.tag < aux->cblen) {
			cb = aux->cb[r->r.tag];
			aux->cb[r->r.tag].fn = NULL;
		}
		if (cb.fn)
			cb.fn(&r->r, cb.aux);
		else if (r->r.type == Rerror)
			fprintf(stderr, "fsdispatch: unhandled Rerror (tag %d): %s\n", (int)r->r.tag, r->r.error);
		//else
		//	fprintf(stderr, "no callback for tag %d\n", r->tag);
		freetag(ctx, r->r.tag);
		free(r);
	}
	write9p(ctx);
}

int
fsflush(C9ctx *ctx, C9tag oldtag)
{
	C9aux *aux;
	C9tag tag;
	struct reply *r, **rp;

	aux = ctx->aux;
	if (c9flush(ctx, &tag, oldtag) != 0)
		return -1;
	r = (struct reply *)fswait(ctx, tag, Rflush);
	if (!r) {
		fprintf(stderr, "Rerror reply to Tflush");
		exit(1);
	}
	free(r);
	freetag(ctx, oldtag);
	if (oldtag < aux->cblen)
		aux->cb[oldtag].fn = NULL;
	for(rp = &aux->queue; (r = *rp); rp = &r->next) {
		if (r->r.tag == oldtag) {
			*rp = r->next;
			free(r);
			break;
		}
	}
	return 0;
}

int
fsattach(C9ctx *ctx, const char *aname)
{
	C9aux *aux;
	C9tag tag;
	C9r *r;
	int fid;

	aux = ctx->aux;
	fid = numget(&aux->fid);
	if (fid < 0) {
		perror("fsattach");
		return -1;
	}
	if (c9attach(ctx, &tag, fid, C9nofid, NULL, aname) != 0)
		return -1;
	r = fswait(ctx, tag, Rattach);
	if (!r) {
		numput(&aux->fid, fid);
		return -1;
	}
	free(r);
	return fid;
}

int
fswalk(C9ctx *ctx, C9tag *tagp, int fid, const char *path[])
{
	C9aux *aux;
	C9tag tag;
	C9r *r;
	int newfid;

	aux = ctx->aux;
	newfid = numget(&aux->fid);
	if (newfid < 0) {
		if (tagp)
			*tagp = NOTAG;
		snprintf(aux->err, sizeof aux->err, "%s", strerror(errno));
		return -1;
	}
	if (c9walk(ctx, &tag, fid, newfid, path) != 0)
		goto error;
	if (tagp) {
		*tagp = tag;
		return newfid;
	}
	r = fswait(ctx, tag, Rwalk);
	if (!r)
		goto error;
	free(r);
	return newfid;

error:
	numput(&aux->fid, newfid);
	if (tagp)
		*tagp = NOTAG;
	return -1;
}

int
fsopen(C9ctx *ctx, C9tag *tagp, int fid, C9mode mode)
{
	C9tag tag;
	C9r *r;

	if (tagp)
		*tagp = NOTAG;
	if (c9open(ctx, &tag, fid, mode) != 0)
		return -1;
	if (tagp) {
		*tagp = tag;
		return 0;
	}
	r = fswait(ctx, tag, Ropen);
	if (!r)
		return -1;
	free(r);
	return 0;
}

int
fsread(C9ctx *ctx, C9tag *tagp, C9r **rp, int fid, uint64_t off, uint32_t len)
{
	C9tag tag;
	C9r *r;

	assert(tagp || rp);
	if (tagp)
		*tagp = NOTAG;
	if (c9read(ctx, &tag, fid, off, len) != 0)
		return -1;
	if (tagp) {
		*tagp = tag;
		return 0;
	}
	r = fswait(ctx, tag, Rread);
	if (!r)
		return -1;
	*rp = r;
	return 0;
}

int
fswrite(C9ctx *ctx, C9tag *tagp, int fid, uint64_t off, const void *buf, uint32_t len)
{
	C9tag tag;
	C9r *r;

	if (tagp)
		*tagp = NOTAG;
	if (c9write(ctx, &tag, fid, off, buf, len) != 0)
		return -1;
	if (tagp) {
		*tagp = tag;
		return 0;
	}
	r = fswait(ctx, tag, Rwrite);
	if (!r)
		return -1;
	free(r);
	return 0;
}

int
fsclunk(C9ctx *ctx, int fid)
{
	C9tag tag;
	C9r *r;

	if (c9clunk(ctx, &tag, fid) != 0)
		return -1;
	r = fswait(ctx, tag, Rclunk);
	if (!r)
		return -1;
	free(r);
	return 0;
}
