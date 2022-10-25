/* SPDX-License-Identifier: ISC */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include "util.h"

#define ENTBIT (sizeof *((struct numtab *)0)->ent * CHAR_BIT)

int
numget(struct numtab *tab)
{
	unsigned long *ent, mask;
	int num;

	for (ent = tab->ent; ent < tab->ent + tab->len; ent++) {
		if (*ent) {
			num = (ent - tab->ent) * ENTBIT;
			for (mask = *ent; (mask & 1) == 0; mask >>= 1)
				++num;
			*ent &= ~(1ul << num);
			return num;
		}
	}
	ent = realloc(tab->ent, (tab->len + 1) * sizeof *ent);
	if (!ent)
		return -1;
	tab->ent = ent;
	ent += tab->len;
	*ent = -2ul;
	num = tab->len * ENTBIT;
	++tab->len;
	return num;
}

int
numput(struct numtab *tab, int num)
{
	size_t index;
	unsigned long mask;

	if (num < 0)
		return -1;
	index = num / ENTBIT;
	mask = 1ul << num % ENTBIT;
	if (index >= tab->len || tab->ent[index] & mask)
		return -1;
	tab->ent[index] |= mask;
	return 0;
}

size_t
utf8dec(uint_least32_t *c, const unsigned char *s, size_t n)
{
	size_t i, l;
	unsigned char b;
	uint_least32_t x;

	b = s[0];
	if (b < 0x80) {
		*c = b;
		return 1;
	}
	if ((b & 0xe0) == 0xc0) {
		x = b & 0x1f;
		l = 2;
	} else if ((b & 0xf0) == 0xe0) {
		x = b & 0x0f;
		l = 3;
	} else if ((b & 0xf8) == 0xf0) {
		x = b & 0x07;
		l = 4;
	} else {
		return -1;
	}
	if (n < l)
		return -1;
	for (i = 1; i < l; ++i) {
		b = *++s;
		if ((b & 0xc0) != 0x80)
			return -1;
		x = x << 6 | b & 0x3f;
	}
	if (x >= 0x110000 || x - 0xd800 < 0x0200)
		return -1;
	*c = x;
	return l;
}
