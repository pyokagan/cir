#include "cir_internal.h"

uint32_t
CirIkind_size(uint32_t ikind, const CirMachine *mach)
{
    switch (ikind) {
    case CIR_ICHAR:
    case CIR_ISCHAR:
    case CIR_IUCHAR:
        return 1;
    case CIR_IBOOL:
        return mach->sizeofBool;
    case CIR_IINT:
    case CIR_IUINT:
        return mach->sizeofInt;
    case CIR_ISHORT:
    case CIR_IUSHORT:
        return mach->sizeofShort;
    case CIR_ILONG:
    case CIR_IULONG:
        return mach->sizeofLong;
    case CIR_ILONGLONG:
    case CIR_IULONGLONG:
        return mach->sizeofLongLong;
    default:
        cir_bug("invalid ikind");
    }
}

bool
CirIkind_isSigned(uint32_t ikind, const CirMachine *mach)
{
    switch (ikind) {
    case CIR_ICHAR:
        return !mach->charIsUnsigned;
    case CIR_ISCHAR:
    case CIR_ISHORT:
    case CIR_IINT:
    case CIR_ILONG:
    case CIR_ILONGLONG:
        return true;
    case CIR_IBOOL:
    case CIR_IUCHAR:
    case CIR_IUSHORT:
    case CIR_IUINT:
    case CIR_IULONG:
    case CIR_IULONGLONG:
        return false;
    default:
        cir_bug("invalid ikind");
    }
}

uint32_t
CirIkind_toUnsigned(uint32_t ikind)
{
    switch (ikind) {
    case CIR_ICHAR:
    case CIR_ISCHAR:
        return CIR_IUCHAR;
    case CIR_ISHORT:
        return CIR_IUSHORT;
    case CIR_IINT:
        return CIR_IUINT;
    case CIR_ILONG:
        return CIR_IULONG;
    case CIR_ILONGLONG:
        return CIR_IULONGLONG;
    default:
        return ikind;
    }
}

uint32_t
CirIkind_fromSize(uint32_t size, bool _unsigned, const CirMachine *mach)
{
    if (size == 1) {
        return _unsigned ? CIR_IUCHAR : CIR_ISCHAR;
    } else if (size == mach->sizeofShort) {
        return _unsigned ? CIR_IUSHORT : CIR_ISHORT;
    } else if (size == mach->sizeofInt) {
        return _unsigned ? CIR_IUINT : CIR_IINT;
    } else if (size == mach->sizeofLong) {
        return _unsigned ? CIR_IULONG : CIR_ILONG;
    } else if (size == mach->sizeofLongLong) {
        return _unsigned ? CIR_IULONGLONG : CIR_ILONGLONG;
    } else {
        cir_bug("CirIkind_fromSize: cannot match size");
    }
}
