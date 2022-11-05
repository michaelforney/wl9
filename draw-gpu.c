#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <wayland-server-core.h>
#include <xf86drm.h>
#include "virtgpu_drm.h"
#include <poll.h>
#include "draw.h"
#include "util.h"

static int fd;

struct image {
	uint32_t bo;
	unsigned char *data;
	size_t size, stride;
	struct wl_listener destroy;
};

static void
imageclose(struct image *i)
{
	struct drm_gem_close close;

	if (i->bo) {
		close.handle = i->bo;
		drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
		i->bo = 0;
	}
	if (i->data) {
		munmap(i->data, i->size);
		i->data = NULL;
	}
}

static void
destroyed(struct wl_listener *l, void *p)
{
	struct image *i;

	i = wl_container_of(l, i, destroy);
	imageclose(i);
	free(i);
}

int
drawgpuattach(struct wl_resource *r, const char *name, int w, int h)
{
	static int id = 2;
	struct drm_virtgpu_resource_create_blob blob;
	struct drm_virtgpu_map map;
	struct wl_listener *l;
	struct image *i;
	unsigned char cmd[64];
	size_t namelen;

	l = wl_resource_get_destroy_listener(r, destroyed);
	if (l) {
		i = wl_container_of(l, i, destroy);
		imageclose(i);
	} else {
		i = malloc(sizeof *i);
		if (!i) {
			perror(NULL);
			return -1;
		}
		i->bo = 0;
		i->data = NULL;
		i->destroy.notify = destroyed;
		wl_resource_add_destroy_listener(r, &i->destroy);
	}

	i->stride = w * 4;
	i->size = h * i->stride;

	namelen = strlen(name);
	if (namelen > sizeof cmd - 6)
		goto fail;
	cmd[0] = 'n';
	putle32(cmd + 1, ++id);
	cmd[5] = namelen;
	memcpy(cmd + 6, name, namelen);
	memset(cmd + 6 + namelen, 'v', 3);

	blob.blob_mem = VIRTGPU_BLOB_MEM_HOST3D_GUEST;
	blob.blob_flags = 0;
	blob.size = i->size;
	blob.cmd = (uint64_t)cmd;
	blob.cmd_size = (6 + namelen + 3) & ~3;
	blob.blob_id = id;
	fprintf(stderr, "id %u\n", id);
	if (drmIoctl(fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &blob) != 0) {
		perror("virtgpu create blob");
		goto fail;
	}
	i->bo = blob.bo_handle;

	map.handle = i->bo;
	if (drmIoctl(fd, DRM_IOCTL_VIRTGPU_MAP, &map) != 0) {
		perror("virtgpu map");
		goto fail;
	}
	i->data = mmap(NULL, blob.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (i->data == MAP_FAILED) {
		perror("mmap");
		goto fail;
	}
	return 0;

fail:
	if (i) {
		imageclose(i);
		wl_list_remove(&i->destroy.link);
		free(i);
	}
	return -1;
}

int
drawgpu(struct wl_resource *sr, struct rect r, struct wl_resource *br, int sx, int sy)
{
	struct drm_virtgpu_3d_transfer_to_host xfer;
	struct wl_listener *l;
	struct image *i;
	struct wl_shm_buffer *b;
	unsigned char *d, *s;
	size_t dl, sl, sh, n;
	int y;

	l = wl_resource_get_destroy_listener(sr, destroyed);
	if (!l)
		return -1;
	i = wl_container_of(l, i, destroy);
	b = wl_shm_buffer_get(br);
	if (!b) {
		fprintf(stderr, "unsupported buffer type %s", wl_resource_get_class(br));
		return -1;
	}
	dl = i->stride;
	sl = wl_shm_buffer_get_stride(b);
	sh = wl_shm_buffer_get_height(b);
	assert(r.x1 * 4 <= dl);
	assert(r.y1 * dl <= i->size);
	n = (r.x1 - r.x0) * 4;
	if (sl < n || sl < sx * 4 || sh < sy + r.y1 - r.y0) {
		fprintf(stderr, "attached buffer is too small\n");
		return -1;
	}
	d = i->data + r.y0 * dl + r.x0 * 4;
	s = wl_shm_buffer_get_data(b);
	s += sy * sl + sx * 4;
	y = r.y0;		
	for (;;) {
		memcpy(d, s, n);
		if (++y == r.y1)
			break;
		d += dl;
		s += sl;
	}
	memset(&xfer, 0, sizeof xfer);
	xfer.bo_handle = i->bo;
	xfer.box.x = r.x0;
	xfer.box.y = r.y0;
	xfer.box.w = r.x1 - r.x0;
	xfer.box.h = r.y1 - r.y0;
	xfer.stride = dl;
	if (drmIoctl(fd, DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST, &xfer) != 0) {
		perror(NULL);
		return -1;
	}
	wl_signal_emit(&drawdone, sr);
	return 0;
}

int
drawgpuinit(void)
{
	fd = drmOpenWithType("virtio_gpu", NULL, DRM_NODE_RENDER);
	if (fd < 0)
		return -1;
	drawattach = drawgpuattach;
	draw = drawgpu;
	wl_signal_init(&drawdone);
	return 0;
}
