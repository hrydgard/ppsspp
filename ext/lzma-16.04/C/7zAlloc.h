/* 7zAlloc.h -- Allocation functions
2013-03-25 : Igor Pavlov : Public domain */

#ifndef __7Z_ALLOC_H
#define __7Z_ALLOC_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void *SzAlloc(void *p, size_t size);
void SzFree(void *p, void *address);

void *SzAllocTemp(void *p, size_t size);
void SzFreeTemp(void *p, void *address);

#ifdef __cplusplus
}
#endif

#endif
