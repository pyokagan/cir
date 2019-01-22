#ifndef CIRQ_H
#define CIRQ_H
#include "cir.h"

static CirVarId
CirQ__varid(CirCodeId code_id)
{
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("CirQ__varid: no value");
    return CirValue_getVar(value);
}

static CirCodeId
CirQ(CirCodeId code_id)
{
    CirStmtId stmt_id;
    CirCodeId outCode_id = CirCode_ofExpr(NULL);
    const CirValue *args[2];

    // Gen: CirCode_ofExpr(NULL)
    CirVarId codeVar_id = CirVar_new(outCode_id);
    CirVar_setType(codeVar_id, CirType_int(CIR_IUINT));
    const CirValue *codeVar_value = CirValue_ofVar(codeVar_id);
    stmt_id = CirCode_appendNewStmt(outCode_id);
    args[0] = CirValue_ofU64(CIR_IINT, 0);
    CirStmt_toCall(stmt_id, codeVar_value, CirValue_ofVar(@CirQ__varid(CirCode_ofExpr)), args, 1);
    CirCode_setValue(outCode_id, codeVar_value);

    return outCode_id;
}

#endif // CIRQ_H
