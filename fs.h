/* SPDX-License-Identifier: ISC */
#define BUFSIZE (32*1024ul)  /* maximum I/O size of virtio-serial */
#define IOHDRSZ 24

struct C9aux {
	int rfd, wfd;
	uint8_t rbuf[BUFSIZE], *rpos, *rend;
	uint8_t wbuf[BUFSIZE], *wpos, *wend;
	char err[128];
	struct numtab tag;
	struct numtab fid;
	struct reply *queue;
	int ready;
	struct callback *cb;
	size_t cblen;
	struct wl_event_source *idle;
};

int fsinit(C9ctx *ctx, C9aux *aux);
int fsasync(C9ctx *ctx, C9tag tag, void (*fn)(C9r *, void *), void *aux);
C9r *fswait(C9ctx *ctx, C9tag tag, C9rtype type);
int fsready(int fd, uint32_t mask, void *ctx);
void fsdispatch(C9ctx *ctx);

int fsflush(C9ctx *ctx, C9tag oldtag);
int fsattach(C9ctx *ctx, const char *aname);
int fswalk(C9ctx *ctx, C9tag *tagp, int fid, const char *path[]);
int fsopen(C9ctx *ctx, C9tag *tagp, int fid, C9mode mode);
int fsread(C9ctx *ctx, C9tag *tagp, C9r **rp, int fid, uint64_t off, uint32_t len);
int fswrite(C9ctx *ctx, C9tag *tagp, int fid, uint64_t off, const void *buf, uint32_t len);
int fsclunk(C9ctx *ctx, int fid);
