#include "cir_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdalign.h>

#define ALLOC_SIZE (1024 * 1024 * 1)

static uint8_t *currMem;
static size_t bytesAllocated;

static size_t
toAlign(size_t n, size_t roundto)
{
    return (n + (roundto - 1U)) & (~(roundto - 1U));
}

void *
cir__xalloc(size_t n)
{
    void *ptr = malloc(n);
    if (!ptr)
        cir_fatal("out of memory when trying to alloc %lu bytes", (unsigned long)n);
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
cir__balloc(size_t n)
{
    n = toAlign(n, CIR_MEM_ALIGN);
    if (!currMem || bytesAllocated + n > ALLOC_SIZE) {
        // Allocate another pool
        currMem = cir__xalloc(ALLOC_SIZE);
        bytesAllocated = 0;
    }
    void *ptr = currMem + bytesAllocated;
    bytesAllocated += n;
    return ptr;
}
