#ifndef ERRORS_H
#define ERRORS_H

#include <errno.h>
#include <stdlib.h>

#include "wlchewing.h"

[[maybe_unused]] static inline int must_errno(int ret, const char *op) {
	if (ret < 0) {
		wlchewing_perr("Failed to %s", op);
		exit(EXIT_FAILURE);
	}
	return ret;
}

[[maybe_unused]] static inline int errnoify(int ret) {
	if (ret < 0) {
		errno = -ret;
	}
	return ret;
}

#endif
