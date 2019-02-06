#include "cir_internal.h"

CIR_PRIVATE
size_t
CirHash_str(const char *str)
{
    size_t hash = 5381;
    uint8_t c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}
