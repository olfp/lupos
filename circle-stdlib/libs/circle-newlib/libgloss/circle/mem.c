/*
 * mem.c
 *
 * *_r wrappers for memory management functions, that simply call
 * the Circle memory management functions. Reentrancy is handled
 * in Circle.
 */


#include "config.h"
#include <malloc.h>

void *_malloc_r(struct _reent *r, size_t size)
{
	(void) r;

	return malloc(size);
}

void _free_r(struct _reent *r, void *p)
{
	(void) r;

	free(p);
}

void *_realloc_r(struct _reent *r, void *p, size_t size)
{
	(void) r;

	return realloc(p, size);
}

void *_calloc_r(struct _reent *r, size_t nelem, size_t elsize)
{
	(void) r;

	return calloc(nelem, elsize);
}
