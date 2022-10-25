#include <stdio.h>
#include <stdint.h>

int writekeymap(FILE *f, int (*nextline)(void *, char **, size_t *), void *aux);
uint32_t keymapmod(uint32_t key);
