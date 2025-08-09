#ifndef XMEM_H
#define XMEM_H

#include <stddef.h>

#define xcalloc(...) _xcalloc(__FILE__, __LINE__, ##__VA_ARGS__)
#define xstrdup(...) _xstrdup(__FILE__, __LINE__, ##__VA_ARGS__)

void *_xcalloc(const char *f, int l, size_t nmemb, size_t size);
char *_xstrdup(const char *f, int l, const char *s);

#endif
