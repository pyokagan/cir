#include "cir_internal.h"

uint32_t
CirFkind_size(uint32_t fkind, const CirMachine *mach)
{
    switch (fkind) {
    case CIR_FFLOAT:
        return mach->sizeofFloat;
    case CIR_FDOUBLE:
        return mach->sizeofDouble;
    case CIR_FLONGDOUBLE:
        return mach->sizeofLongDouble;
    default:
        cir_bug("invalid fkind");
    }
}

uint32_t
CirFkind_fromSize(uint32_t size, const CirMachine *mach)
{
    if (size == mach->sizeofFloat)
        return CIR_FFLOAT;
    else if (size == mach->sizeofDouble)
        return CIR_FDOUBLE;
    else if (size == mach->sizeofLongDouble)
        return CIR_FLONGDOUBLE;
    else
        cir_fatal("cannot find a fkind with size %u", (unsigned)size);
}
