#ifndef XMEM_H
#define XMEM_H

#include <stddef.h>
#include <stdlib.h>

#include "wlchewing.h"

#define xcalloc(...) _xcalloc(__FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)
#define xstrdup(...) _xstrdup(__FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)

static inline void *assert_pointer(const char *f, int l, const char *op, void *p) {
	if (p == NULL) {
		_wlchewing_errloc("Fail to %s", f, l, op);
		exit(EXIT_FAILURE);
	}
	return p;
}

[[maybe_unused]] static inline void *_xcalloc(const char *f, int l, size_t nmemb, size_t size) {
	return assert_pointer(f, l, &__func__[2], calloc(nmemb, size));
}

[[maybe_unused]] static inline char *_xstrdup(const char *f, int l, const char *s) {
	return assert_pointer(f, l, &__func__[2], strdup(s));
}
#endif
