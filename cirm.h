#ifndef CIRM_H
#define CIRM_H
#include <stdlib.h>
#include "cir.h"

static CirVarId
CirM_varid(CirCodeId codeId)
{
    if (CirCode_getFirstStmt(codeId))
        cir_fatal("CirM_varid: code is non-empty");
    if (CirCode_getNumVars(codeId))
        cir_fatal("CirM_varid: code owns vars");
    const CirValue *value = CirCode_getValue(codeId);
    if (!value)
        cir_fatal("CirM_varid: code has no value");
    return CirValue_getVar(value);
}

static CirCodeId
CirM_type(CirCodeId codeId)
{
    const CirType *dummyVar;

    if (CirCode_getFirstStmt(codeId))
        cir_fatal("CirM_type: code is non-empty");
    if (CirCode_getNumVars(codeId))
        cir_fatal("CirM_type: code owns vars");
    const CirValue *value = CirCode_getValue(codeId);
    if (!value)
        cir_fatal("CirM_type: code has no value");
    const CirType *type = CirValue_getTypeValue(value);
    const CirType *typeType = CirVar_getType(@CirM_varid(dummyVar));
    return CirCode_ofExpr(CirValue_withCastType(CirValue_ofU64(CIR_IULONG, (uint64_t)type), typeType));
}

static CirCodeId
CirM_value(CirCodeId codeId)
{
    if (CirCode_getFirstStmt(codeId))
        cir_fatal("CirM_value: code is non-empty");
    if (CirCode_getNumVars(codeId))
        cir_fatal("CirM_value: code owns vars");
    const CirValue *value = CirCode_getValue(codeId);
    if (!value)
        cir_fatal("CirM_value: code has no value");
    return CirCode_ofExpr(CirValue_withCastType(CirValue_ofU64(CIR_IULONG, (uint64_t)value), @CirM_type(__typeval(const CirValue *))));
}

#endif // CIRM_H
