#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wlchewing.h"
#include "xmem.h"

static inline void *assert_pointer(const char *f, int l, const char *op, void *p) {
	if (p == NULL) {
		_wlchewing_errloc("Fail to %s", f, l, op);
		exit(EXIT_FAILURE);
	}
	return p;
}

void *_xcalloc(const char *f, int l, size_t nmemb, size_t size) {
	return assert_pointer(f, l, &__func__[2], calloc(nmemb, size));
}

char *_xstrdup(const char *f, int l, const char *s) {
	return assert_pointer(f, l, &__func__[2], strdup(s));
}
