/*
 * Copyright (c) 2015 Scarletts <scarlett@entering.space>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "xalloc.h"

void *
xmalloc(size_t sz)
{
	void *ptr;

	ptr = malloc(sz);
	if (ptr == NULL) {
		perror("malloc");
		exit(1);
	}
	return ptr;
}

void *
xrealloc(void *optr, size_t sz)
{
	void *ptr;

	ptr = realloc(optr, sz);
	if (ptr == NULL) {
		perror("realloc");
		exit(1);
	}
	return ptr;
}

void *
xreallocarray(void *optr, size_t x, size_t y)
{
	if (x != 0 && y > SIZE_MAX / x) {
		fprintf(stderr,
		    "xreallocarray: multiplication will overflow\n");
	}
	return xrealloc(optr, x * y);
}

char *
xstrdup(const char *str)
{
	char *ptr;

	ptr = strdup(str);
	if (ptr == NULL) {
		perror("strdup");
		exit(1);
	}
	return ptr;
}

int
xasprintf(char **out, const char *fmt, ...)
{
	char *str = NULL;
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (ret < 0) {
		perror("vsnprintf");
		exit(1);
	}

	str = xmalloc((size_t)ret + 1);

	va_start(ap, fmt);
	ret = vsnprintf(str, (size_t)ret + 1, fmt, ap);
	va_end(ap);
	if (ret < 0) {
		perror("vsnprintf");
		exit(1);
	}
	*out = str;
	return ret;
}

