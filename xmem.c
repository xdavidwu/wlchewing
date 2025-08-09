#include "xmem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void *assert_pointer(void *p, const char *op) {
	if (p == NULL) {
		perror(op);
		exit(EXIT_FAILURE);
	}
	return p;
}

void *xcalloc(size_t nmemb, size_t size) {
	return assert_pointer(calloc(nmemb, size), "calloc");
}

char *xstrdup(const char *s) {
	return assert_pointer(strdup(s), "strdup");
}
