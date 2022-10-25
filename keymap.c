/* SPDX-License-Identifier: ISC */
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEN(a) ((sizeof a) / (sizeof *a))

static const char xkbnames[] =
	"\0"
	"BackSpace\0"
	"Tab\0"
	"Return\0"
	"Escape\0"
	"space\0"
	"exclam\0"
	"quotedbl\0"
	"numbersign\0"
	"dollar\0"
	"percent\0"
	"ampersand\0"
	"apostrophe\0"
	"parenleft\0"
	"parenright\0"
	"asterisk\0"
	"plus\0"
	"comma\0"
	"minus\0"
	"period\0"
	"slash\0"
	"0\0001\0002\0003\0004\0005\0006\0007\0008\0009\0"
	"colon\0"
	"semicolon\0"
	"less\0"
	"equal\0"
	"greater\0"
	"question\0"
	"at\0"
	"A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0"
	"N\0O\0P\0Q\0R\0S\0T\0U\0V\0W\0X\0Y\0Z\0"
	"bracketleft\0"
	"backslash\0"
	"bracketright\0"
	"asciicircum\0"
	"underscore\0"
	"grave\0"
	"a\0b\0c\0d\0e\0f\0g\0h\0i\0j\0k\0l\0m\0"
	"n\0o\0p\0q\0r\0s\0t\0u\0v\0w\0x\0y\0z\0"
	"braceleft\0"
	"bar\0"
	"braceright\0"
	"asciitilde\0"
	"Delete\0"
	"F1\0F2\0F3\0F4\0F5\0F6\0F7\0F8\0F9\0F10\0F11\0F12\0"
	"Home\0"
	"Up\0"
	"Prior\0"
	"Print\0"
	"Left\0"
	"Right\0"
	"Next\0"
	"Insert\0"
	"Alt_L\0"
	"Shift_L\0"
	"Control_L\0"
	"End\0"
	"Scroll_Lock\0"
	"Down\0"
	"Break\0"
	"Caps_Lock\0"
	"Num_Lock\0"
	"Alt_R\0"
	"Super_L";

static const uint_least16_t xkbascii[128] = {
	  0,   0,   0,   0,   0,   0,   0,   0,
	  1,  11,  15,   0,   0,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0,
	  0,   0,   0,  22,   0,   0,   0,   0,
	 29,  35,  42,  51,  62,  69,  77,  87,
	 98, 108, 119, 128, 133, 139, 145, 152,
	158, 160, 162, 164, 166, 168, 170, 172,
	174, 176, 178, 184, 194, 199, 205, 213,
	222, 225, 227, 229, 231, 233, 235, 237,
	239, 241, 243, 245, 247, 249, 251, 253,
	255, 257, 259, 261, 263, 265, 267, 269,
	271, 273, 275, 277, 289, 299, 312, 324,
	335, 341, 343, 345, 347, 349, 351, 353,
	355, 357, 359, 361, 363, 365, 367, 369,
	371, 373, 375, 377, 379, 381, 383, 385,
	387, 389, 391, 393, 403, 407, 418, 429,
};

static const uint_least16_t xkbextra[][2] = {
	{0xF001, 436},  /* KF|1    → F1          */
	{0xF002, 439},  /* KF|2    → F2          */
	{0xF003, 442},  /* KF|3    → F3          */
	{0xF004, 445},  /* KF|4    → F4          */
	{0xF005, 448},  /* KF|5    → F5          */
	{0xF006, 451},  /* KF|6    → F6          */
	{0xF007, 454},  /* KF|7    → F7          */
	{0xF008, 457},  /* KF|8    → F8          */
	{0xF009, 460},  /* KF|9    → F9          */
	{0xF00A, 463},  /* KF|10   → F10         */
	{0xF00B, 467},  /* KF|11   → F11         */
	{0xF00C, 471},  /* KF|12   → F12         */
	{0xF00D, 475},  /* Khome   → Home        */
	{0xF00E, 480},  /* Kup     → Up          */
	{0xF00F, 483},  /* Kpgup   → Prior       */
	{0xF010, 489},  /* Kprint  → Print       */
	{0xF011, 495},  /* Kleft   → Left        */
	{0xF012, 500},  /* Kright  → Right       */
	{0xF013, 506},  /* Kpgdown → Next        */
	{0xF014, 511},  /* Kins    → Insert      */
	{0xF015, 518},  /* Kalt    → Alt_L       */
	{0xF016, 524},  /* Kshift  → Shift_L     */
	{0xF017, 532},  /* Kctl    → Control_L   */
	{0xF018, 542},  /* Kend    → End         */
	{0xF019, 546},  /* Kscroll → Scroll_Lock */
	{0xF800, 558},  /* Kdown   → Down        */
	{0xF861, 563},  /* Kbreak  → Break       */
	{0xF864, 569},  /* Kcaps   → Caps_Lock   */
	{0xF865, 579},  /* Knum    → Num_Lock    */
	{0xF867, 588},  /* Kaltgr  → Alt_R       */
	{0xF868, 594},  /* Kmod4   → Super_L     */
};

static const char xkbtypes[] =
	"xkb_types \"(unnamed)\" {\n"
	"	type \"ONE_LEVEL\" {\n"
	"		modifiers = none;\n"
	"		level_name[1] = \"Any\";\n"
	"	};\n"
	"	type \"TWO_LEVEL\" {\n"
	"		modifiers = Shift;\n"
	"		map[Shift] = 2;\n"
	"		level_name[1] = \"Base\";\n"
	"		level_name[2] = \"Shift\";\n"
	"	};\n"
	"	type \"ALPHABETIC\" {\n"
	"		modifiers = Shift+Lock;\n"
	"		map[Shift] = 2;\n"
	"		map[Lock] = 2;\n"
	"		level_name[1] = \"Base\";\n"
	"		level_name[2] = \"Caps\";\n"
	"	};\n"
	/*
	"	type \"KEYPAD\" {\n"
	"		modifiers = Shift+NumLock;\n"
	"		map[NumLock] = 2;\n"
	"		level_name[1] = \"Base\";\n"
	"		level_name[2] = \"Number\";\n"
	"	};\n"
	*/
	"};\n";

static const char xkbcompat[] =
	"xkb_compat \"(unnamed)\" {\n"
	"	interpret Shift_L {\n"
	"		action = SetMods(modifiers=Shift,clearLocks);\n"
	"	};\n"
	"	interpret Caps_Lock {\n"
	"		action = LockMods(modifiers=Lock);\n"
	"	};\n"
	"};\n";

static const char xkbmods[] =
	"	modifier_map Mod1 {<U+F015>, <U+F867>};\n"
	"	modifier_map Shift {<U+F016>};\n"
	"	modifier_map Control {<U+F017>};\n"
	"	modifier_map Lock {<U+F864>};\n"
	"	modifier_map Mod4 {<U+F868>};\n";

struct key {
	uint_least16_t scan;
	uint_least16_t rune;
	uint_least16_t name[10];
};

static int
u16cmp(const void *a, const void *b)
{
	uint_least16_t ra, rb;

	ra = *(uint_least16_t *)a;
	rb = *(uint_least16_t *)b;
	return ra < rb ? -1 : ra > rb;
}

static unsigned
runetoxkb(unsigned long r)
{
	uint_least16_t *k;

	if (r > 0xFFFF)
		return 0;
	if (r < 128)
		return xkbascii[r];
	k = bsearch(&(uint_least16_t){r}, xkbextra, LEN(xkbextra), sizeof *xkbextra, u16cmp);
	return k ? k[1] : 0;
}

int
writekeymap(FILE *f, int (*nextline)(void *, char **, size_t *), void *aux)
{
	char *pos;
	size_t len;
	int ret;
	unsigned long level, scan, rune;
	unsigned name;
	struct key *keys, *k;
	size_t keyslen, i;

	fputs("xkb_keymap {\n", f);
	fputs("xkb_keycodes \"(unnamed)\" {\n", f);
	keys = NULL;
	keyslen = 0;
	for (;;) {
		ret = nextline(aux, &pos, &len);
		if (ret < 0) {
			free(keys);
			return -1;
		}
		if (ret == 0)
			break;
		level = strtoul(pos, &pos, 10);
		scan = strtoul(pos, &pos, 10);
		rune = strtoul(pos, &pos, 10);
		name = runetoxkb(rune);
		if (!name)
			continue;
		if (level == 2)
			scan |= 0x8000;
		if (level == 0 || level == 2) {
			for (k = keys; k < keys + keyslen; ++k) {
				if (k->rune == rune)
					break;
			}
			if (k < keys + keyslen) {
				/*
				we can't distinguish between scan
				codes that map to the same rune at
				level 0, so use the first one
				*/
				//fprintf(stderr, "duplicate key %lu %lu %s\n", level, scan, xkbnames + name);
				continue;
			}
			if ((keyslen & keyslen - 1) == 0 && keyslen - 1 > 127) {
				keys = realloc(keys, (keyslen ? keyslen * 2 : 128) * sizeof *keys);
				if (!keys) {
					perror(NULL);
					return -1;
				}
			}
			k = &keys[keyslen++];
			k->scan = scan;
			k->rune = rune;
			k->name[0] = name;
			memset(k->name + 1, 0, sizeof k->name - sizeof k->name[0]);
			fprintf(f, "\t<U+%.4lX> = 0x%.4lX;\n", rune, rune + 8);
		} else {
			k = bsearch(&(uint_least16_t){scan}, keys, keyslen, sizeof *keys, u16cmp);
			if (!k)
				continue;  /* no unshifted rune */
			if (level == 5 || level == 6)
				continue;  /* levels involving esc1 */
			k->name[level] = runetoxkb(rune);
		}
	}
	fputs("};\n", f);
	fwrite(xkbtypes, 1, sizeof xkbtypes - 1, f);
	fwrite(xkbcompat, 1, sizeof xkbcompat - 1, f);
	fputs("xkb_symbols \"(unnamed)\" {\n", f);
	for (i = 0; i < keyslen; ++i) {
		k = &keys[i];
		fprintf(f, "\tkey <U+%.4"PRIXLEAST32"> {[%s", k->rune, xkbnames + k->name[0]);
		if (k->name[1])
			fprintf(f, ", %s", xkbnames + k->name[1]);
		fprintf(f, "]};\n");
	}
	fwrite(xkbmods, 1, sizeof xkbmods - 1, f);
	fputs("};\n", f);
	fputs("};\n", f);
	fputc('\0', f);
	fflush(f);
	free(keys);
	if (ferror(f)) {
		perror("write");
		return -1;
	}
	return 0;
}

uint32_t
keymapmod(uint32_t key)
{
	switch (key) {
	case 0xF015: return 1 << 3;  /* Kalt   → Mod1    */
	case 0xF016: return 1 << 0;  /* Kshift → Shift   */
	case 0xF017: return 1 << 2;  /* Kctl   → Control */
	case 0xF864: return 1 << 1;  /* Kcaps  → Lock    */
	case 0xF867: return 1 << 3;  /* Kaltgr → Mod1    */
	case 0xF868: return 1 << 6;  /* Kmod4  → Mod4    */
	}
	return 0;
}
