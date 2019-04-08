#include "cir_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>
#include <assert.h>

#define ALLOC_SIZE (1024 * 1024 * 1)

static uint8_t *currMem;
static size_t bytesAllocated;

void *
cir__xalloc(size_t n)
{
    void *ptr = malloc(n);
    if (!ptr)
        cir_fatal("out of memory when trying to alloc %lu bytes", (unsigned long)n);
    return ptr;
}

void *
cir__zalloc(size_t n)
{
    void *ptr = calloc(1, n);
    if (!ptr)
        cir_fatal("out of memory when trying to zalloc %lu bytes", (unsigned long)n);
    return ptr;
}

void *
cir__xrealloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if (!ptr)
        cir_fatal("out of memory when trying to realloc %lu bytes", (unsigned long)size);
    return ptr;
}

void
cir__xfree(void *ptr)
{
    free(ptr);
}

void *
CirMem_balloc(size_t n, size_t align)
{
    assert(align);
    if (n % align)
        n += align - n % align;
    if (!currMem || bytesAllocated + n > ALLOC_SIZE) {
        // Allocate another pool
        currMem = cir__xalloc(ALLOC_SIZE);
        bytesAllocated = 0;
    }
    void *ptr = currMem + bytesAllocated;
    bytesAllocated += n;
    return ptr;
}
