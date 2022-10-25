/* SPDX-License-Identifier: ISC */
#include <stddef.h>
#include <stdint.h>

#define LEN(a) (sizeof (a) / sizeof *(a))

struct numtab {
	unsigned long *ent;
	size_t len;
};

int numget(struct numtab *tab);
int numput(struct numtab *tab, int num);

size_t utf8dec(uint_least32_t *c, const unsigned char *s, size_t n);

static inline void *
putle16(void *p, unsigned v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	return b + 2;
}

static inline void *
putle32(void *p, unsigned long v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v >> 16 & 0xff;
	b[3] = v >> 24 & 0xff;
	return b + 4;
}

static inline void *
putle64(void *p, unsigned long long v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v >> 16 & 0xff;
	b[3] = v >> 24 & 0xff;
	b[4] = v >> 32 & 0xff;
	b[5] = v >> 40 & 0xff;
	b[6] = v >> 48 & 0xff;
	b[7] = v >> 56 & 0xff;
	return b + 8;
}

static inline unsigned
getle16(void *p)
{
	unsigned char *b = p;
	unsigned v;

	v = b[0] & 0xffu;
	v |= (b[1] & 0xffu) << 8;
	return v;
}

static inline unsigned long
getle32(void *p)
{
	unsigned char *b = p;
	unsigned long v;

	v = b[0] & 0xfful;
	v |= (b[1] & 0xfful) << 8;
	v |= (b[2] & 0xfful) << 16;
	v |= (b[3] & 0xfful) << 24;
	return v;
}
