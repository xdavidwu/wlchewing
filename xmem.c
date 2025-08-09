#include "xmem.h"

#include <stdio.h>
#include <stdlib.h>

void *xcalloc(size_t nmemb, size_t size) {
	void *res = calloc(nmemb, size);
	if (res == NULL) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	return res;
}
