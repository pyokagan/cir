#include "cir_internal.h"
#include <limits.h>

CirMachine CirMachine__host;
CirMachine CirMachine__build;

enum dummy_enum {
    DUMMY_ENUM
};

void
CirMachine__initBuiltin(CirMachine *mach)
{
#if defined(__GNUC__) || defined(__clang__)
    mach->compiler = CIR_GCC;
#elif defined(_MSC_VER)
    mach->compiler = CIR_MSVC;
#else
#error "Unknown compiler"
#endif

    mach->sizeofShort = sizeof(short);
    mach->sizeofInt = sizeof(int);
    mach->sizeofBool = sizeof(_Bool);
    mach->sizeofLong = sizeof(long);
    mach->sizeofLongLong = sizeof(long long);
    mach->sizeofPtr = sizeof(void *);
    mach->sizeofFloat = sizeof(float);
    mach->sizeofDouble = sizeof(double);
    mach->sizeofLongDouble = sizeof(long double);
    mach->sizeofFloat128 = sizeof(_Float128);
    mach->sizeofVoid = sizeof(void);
    mach->sizeofFun = sizeof(int(void));
    mach->sizeofSizeT = sizeof(size_t);
    mach->alignofShort = _Alignof(short);
    mach->alignofInt = _Alignof(int);
    mach->alignofBool = _Alignof(_Bool);
    mach->alignofLong = _Alignof(long);
    mach->alignofLongLong = _Alignof(long long);
    mach->alignofPtr = _Alignof(void *);
    mach->alignofEnum = _Alignof(enum dummy_enum);
    mach->alignofFloat = _Alignof(float);
    mach->alignofDouble = _Alignof(double);
    mach->alignofLongDouble = _Alignof(long double);
    mach->alignofFloat128 = _Alignof(_Float128);
    mach->alignofFun = _Alignof(int(void));
#if defined(CHAR_MIN)
#if CHAR_MIN < 0
    mach->charIsUnsigned = false;
#else
    mach->charIsUnsigned = true;
#endif
#else
#error "CHAR_MIN not defined"
#endif
}

void
CirMachine__logCompiler(uint32_t compiler)
{
    switch (compiler) {
    case CIR_GCC:
        CirLog_print("GCC");
        return;
    case CIR_MSVC:
        CirLog_print("MSVC");
        return;
    default:
        cir_bug("unknown compiler");
    }
}

void
CirMachine__log(const CirMachine *mach)
{
    CirLog_print("compiler = ");
    CirMachine__logCompiler(mach->compiler);
    CirLog_print("\n");
    CirLog_printf("sizeofShort = %u\n", mach->sizeofShort);
    CirLog_printf("sizeofInt = %u\n", mach->sizeofInt);
    CirLog_printf("sizeofBool = %u\n", mach->sizeofBool);
    CirLog_printf("sizeofLong = %u\n", mach->sizeofLong);
    CirLog_printf("sizeofLongLong = %u\n", mach->sizeofLongLong);
    CirLog_printf("sizeofPtr = %u\n", mach->sizeofPtr);
    CirLog_printf("sizeofFloat = %u\n", mach->sizeofFloat);
    CirLog_printf("sizeofDouble = %u\n", mach->sizeofDouble);
    CirLog_printf("sizeofLongDouble = %u\n", mach->sizeofLongDouble);
    CirLog_printf("sizeofVoid = %u\n", mach->sizeofVoid);
    CirLog_printf("sizeofFun = %u\n", mach->sizeofFun);
    CirLog_printf("sizeofSizeT = %u\n", mach->sizeofSizeT);
    CirLog_printf("alignofShort = %u\n", mach->alignofShort);
    CirLog_printf("alignofInt = %u\n", mach->alignofInt);
    CirLog_printf("alignofBool = %u\n", mach->alignofBool);
    CirLog_printf("alignofLong = %u\n", mach->alignofLong);
    CirLog_printf("alignofLongLong = %u\n", mach->alignofLongLong);
    CirLog_printf("alignofPtr = %u\n", mach->alignofPtr);
    CirLog_printf("alignofEnum = %u\n", mach->alignofEnum);
    CirLog_printf("alignofFloat = %u\n", mach->alignofFloat);
    CirLog_printf("alignofDouble = %u\n", mach->alignofDouble);
    CirLog_printf("alignofLongDouble = %u\n", mach->alignofLongDouble);
    CirLog_printf("alignofFun = %u\n", mach->alignofLongDouble);
    CirLog_printf("charIsUnsigned = %s\n", mach->charIsUnsigned ? "true" : "false");
}

const CirMachine *
CirMachine_getBuild(void)
{
    return &CirMachine__build;
}

const CirMachine *
CirMachine_getHost(void)
{
    return &CirMachine__host;
}
