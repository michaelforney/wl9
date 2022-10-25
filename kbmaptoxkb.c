/* SPDX-License-Identifier: ISC */
#define _POSIX_C_SOURCE 200809L
#include "keymap.h"

static int
nextline(void *aux, char **str, size_t *len)
{
	static char *buf;
	static size_t buflen;
	ssize_t ret;

	ret = getline(&buf, &buflen, stdin);
	if (ret < 0)
		return ferror(stdin) ? -1 : 0;
	if (buf[ret - 1] == '\n')
		--ret;
	buf[ret] = '\0';
	*str = buf;
	*len = ret;
	return 1;
}

int
main(void)
{
	return writekeymap(stdout, nextline, NULL) == 0;
}
