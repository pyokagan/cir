#include "cir_internal.h"

void
CirArray__alloc(CirGenericArray *arr, size_t elemSize, size_t n)
{
    size_t new_alloc, new_alloc_bytes;

    if (arr->alloc > n)
        return; // we have enough space

    if (arr->alloc) {
        new_alloc = (arr->alloc * 3) / 2;
    } else {
        new_alloc = 64;
    }

    if (new_alloc < n)
        new_alloc = n;

    new_alloc_bytes = new_alloc * elemSize;

    arr->items = cir__xrealloc(arr->items, new_alloc_bytes);
    arr->alloc = new_alloc;
}

void
CirArray__grow(CirGenericArray *arr, size_t elemSize, size_t n)
{
    size_t new_len = arr->len + n;
    return CirArray__alloc(arr, elemSize, new_len);
}
