#include "cir_internal.h"
#include <assert.h>

static struct {
    CirName name;
    const CirType *type;
} builtins[CIR_NUM_BUILTINS];

void
CirBuiltin_init(const CirMachine *mach)
{
    const CirType *sizedIntType;
    CirFunParam params[1] = {};

    // bswap16
    builtins[CIR_BUILTIN_BSWAP16].name = CirName_of("__builtin_bswap16");
    sizedIntType = CirType_int(CirIkind_fromSize(2, false, mach));
    params[0].type = sizedIntType;
    builtins[CIR_BUILTIN_BSWAP16].type = CirType_fun(sizedIntType, params, 1, false);

    // bswap32
    builtins[CIR_BUILTIN_BSWAP32].name = CirName_of("__builtin_bswap32");
    sizedIntType = CirType_int(CirIkind_fromSize(4, false, mach));
    params[0].type = sizedIntType;
    builtins[CIR_BUILTIN_BSWAP32].type = CirType_fun(sizedIntType, params, 1, false);

    // bswap64
    builtins[CIR_BUILTIN_BSWAP64].name = CirName_of("__builtin_bswap64");
    sizedIntType = CirType_int(CirIkind_fromSize(8, false, mach));
    params[0].type = sizedIntType;
    builtins[CIR_BUILTIN_BSWAP64].type = CirType_fun(sizedIntType, params, 1, false);
}

CirBuiltinId
CirBuiltin_ofName(CirName name)
{
    for (size_t i = 1; i < CIR_NUM_BUILTINS; i++) {
        if (builtins[i].name == name)
            return i;
    }
    return 0;
}

CirName
CirBuiltin_getName(CirBuiltinId builtinId)
{
    assert(builtinId && builtinId < CIR_NUM_BUILTINS);
    return builtins[builtinId].name;
}

const CirType *
CirBuiltin_getType(CirBuiltinId builtinId)
{
    assert(builtinId && builtinId < CIR_NUM_BUILTINS);
    return builtins[builtinId].type;
}
