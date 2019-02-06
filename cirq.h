#ifndef CIRQ_H
#define CIRQ_H
#include <stdlib.h>
#include "cir.h"

typedef struct VarIdToVarId {
    CirVarId src, dst;
} VarIdToVarId;

static void
VarIdToVarId_insertItem(VarIdToVarId *table, size_t size, CirVarId src, CirVarId dst)
{
    size_t i;
    for (i = src % size; table[i].src; i = (i + 1) % size);
    table[i].src = src;
    table[i].dst = dst;
}

static CirVarId
VarIdToVarId_lookup(VarIdToVarId *table, size_t size, CirVarId src)
{
    for (size_t i = src % size; table[i].src; i = (i + 1) % size) {
        if (table[i].src == src)
            return table[i].dst;
    }
    return 0;
}

typedef struct StmtInfo {
    CirStmtId key;
    CirVarId stmtHolderId;
} StmtInfo;

static void
StmtInfo_insertItem(StmtInfo *table, size_t size, CirStmtId key, CirVarId stmtHolderId)
{
    size_t i;
    for (i = key % size; table[i].key; i = (i + 1) % size);
    table[i].key = key;
    table[i].stmtHolderId = stmtHolderId;
}

static CirVarId
StmtInfo_lookupStmtHolderId(StmtInfo *table, size_t size, CirStmtId key)
{
    for (size_t i = key % size; table[i].key; i = (i + 1) % size) {
        if (table[i].key == key)
            return table[i].stmtHolderId;
    }
    cir_fatal("encountered jump to CirStmt outside of CirCode");
}

typedef struct CirQState {
    VarIdToVarId *varTable;
    size_t varTableSize;
    StmtInfo *stmtTable;
    size_t stmtTableSize;
    const CirType *cirValueType;
} CirQState;

static CirVarId
CirQ_varid(CirCodeId code_id)
{
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("CirQ_varid: no value");
    return CirValue_getVar(value);
}

static const CirValue *
CirQ_liftName(CirCodeId outCodeId, CirName name)
{
    return CirValue_ofU64(CIR_IULONG, (uint64_t)name);
}

// inValue: the value to lift
// outValue: value to assign it to
// outCode_id: output code to append to
static const CirValue *
CirQ_liftValue(CirCodeId outCodeId, const CirValue *inValue, CirQState *state)
{
    CirStmtId outStmtId;
    const CirValue *args[1];

    if (!inValue)
        return CirValue_ofU64(CIR_IULONG, 0);

    if (CirValue_isLval(inValue)) {
        CirVarId origVarId = CirValue_getVar(inValue);
        CirVarId newVarId = VarIdToVarId_lookup(state->varTable, state->varTableSize, origVarId);
        if (newVarId) {
            if (CirValue_isMem(inValue))
                cir_bug("TODO: handle mem");
            if (CirValue_getNumFields(inValue))
                cir_bug("TODO: handle fields");

            CirVarId tmpValueId = CirVar_new(outCodeId);
            CirVar_setType(tmpValueId, state->cirValueType);

            // Gen: [tmpValue] = CirValue_ofVar([newVarId])
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = CirValue_ofVar(newVarId);
            CirStmt_toCall(outStmtId, CirValue_ofVar(tmpValueId), CirValue_ofVar(@CirQ_varid(CirValue_ofVar)), args, 1);

            return CirValue_ofVar(tmpValueId);
        }
    }

    return CirValue_ofU64(CIR_IULONG, (uint64_t)inValue);
}

static const CirValue *
CirQ_liftType(CirCodeId outCodeId, const CirType *inType)
{
    return CirValue_ofU64(CIR_IULONG, (uint64_t)inType);
}

static const CirValue *
CirQ_liftStmtId(CirCodeId outCodeId, CirStmtId stmtId, CirQState *state)
{
    if (!stmtId)
        return CirValue_ofU64(CIR_IUINT, 0);

    CirVarId stmtHolderId = StmtInfo_lookupStmtHolderId(state->stmtTable, state->stmtTableSize, stmtId);
    return CirValue_ofVar(stmtHolderId);
}

static CirCodeId
CirQ(CirCodeId tplCodeId)
{
    CirQState state;
    CirCodeId outCodeId = CirCode_ofExpr(NULL);
    CirStmtId outStmtId; // Points to the current statement we are adding to outCodeId
    CirStmtId inStmtId; // Points to the current statement we are examining in tplCodeId
    const CirValue *args[5]; // For use with CirStmt_toCall()

    // Compute some well-known types
    // TODO: Can fix this by allowing compile-time-functions to receive types
    // directly
    const CirType *t;
    t = CirVar_getType(@CirQ_varid(CirCode_ofExpr));
    const CirType *cirCodeType = CirType_getBaseType(t); // CirCodeId
    t = CirVar_getType(@CirQ_varid(CirValue_ofU64));
    const CirType *cirValueType = CirType_getBaseType(t); // const CirValue*
    state.cirValueType = cirValueType;
    t = CirVar_getType(@CirQ_varid(CirCode_appendNewStmt));
    const CirType *cirStmtType = CirType_getBaseType(t); // CirStmtId
    t = CirVar_getType(@CirQ_varid(CirVar_new));
    const CirType *cirVarType = CirType_getBaseType(t); // CirVarId

    // Gen: [codeVarValue] = CirCode_ofExpr(NULL)
    CirVarId codeVarId = CirVar_new(outCodeId);
    CirVar_setType(codeVarId, cirCodeType);
    const CirValue *codeVarValue = CirValue_ofVar(codeVarId);
    outStmtId = CirCode_appendNewStmt(outCodeId);
    args[0] = CirValue_ofU64(CIR_IINT, 0);
    CirStmt_toCall(outStmtId, codeVarValue, CirValue_ofVar(@CirQ_varid(CirCode_ofExpr)), args, 1);
    CirCode_setValue(outCodeId, codeVarValue);

    // Generate new vars for each var in tplCodeId, and assign them to
    // variables
    size_t numVars = CirCode_getNumVars(tplCodeId);
    size_t varTableSize = CirPrime_ge(numVars * 2);
    VarIdToVarId *varTable = calloc(varTableSize, sizeof(VarIdToVarId));
    if (!varTable)
        cir_fatal("out of memory");
    state.varTable = varTable;
    state.varTableSize = varTableSize;
    for (size_t i = 0; i < numVars; i = i + 1) {
        CirVarId origVarId = CirCode_getVar(tplCodeId, i);
        CirVarId newVarHolderId = CirVar_new(outCodeId);
        CirVar_setType(newVarHolderId, cirVarType);
        const CirValue *newVarHolderValue = CirValue_ofVar(newVarHolderId);

        // Gen: [newVarHolder] = CirVar_new([codeVarValue])
        outStmtId = CirCode_appendNewStmt(outCodeId);
        args[0] = codeVarValue;
        CirStmt_toCall(outStmtId, newVarHolderValue, CirValue_ofVar(@CirQ_varid(CirVar_new)), args, 1);

        // Gen: CirVar_setType([newVarHolder], type)
        outStmtId = CirCode_appendNewStmt(outCodeId);
        args[0] = newVarHolderValue;
        args[1] = CirQ_liftType(outCodeId, CirVar_getType(origVarId));
        CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirVar_setType)), args, 2);

        VarIdToVarId_insertItem(varTable, varTableSize, origVarId, newVarHolderId);
    }

    // Compute the total number of stmts and the max number of arguments
    size_t numStmts = 0;
    size_t maxNumArgs = 0;
    for (inStmtId = CirCode_getFirstStmt(tplCodeId); inStmtId; inStmtId = CirStmt_getNext(inStmtId)) {
        if (CirStmt_isCall(inStmtId)) {
            size_t numArgs = CirStmt_getNumArgs(inStmtId);
            if (numArgs > maxNumArgs)
                maxNumArgs = numArgs;
        }
        numStmts = numStmts + 1;
    }

    // Generate statements
    size_t stmtTableSize = CirPrime_ge(numStmts * 2);
    StmtInfo *stmtTable = calloc(stmtTableSize, sizeof(StmtInfo));
    if (!stmtTable)
        cir_fatal("out of memory");
    state.stmtTable = stmtTable;
    state.stmtTableSize = stmtTableSize;
    for (inStmtId = CirCode_getFirstStmt(tplCodeId); inStmtId; inStmtId = CirStmt_getNext(inStmtId)) {
        CirVarId stmtHolderId = CirVar_new(outCodeId);
        CirVar_setType(stmtHolderId, cirStmtType);
        const CirValue *stmtHolderValue = CirValue_ofVar(stmtHolderId);

        // Gen: [stmtHolderValue] = CirCode_appendNewStmt([codeVarValue])
        outStmtId = CirCode_appendNewStmt(outCodeId);
        args[0] = codeVarValue;
        CirStmt_toCall(outStmtId, stmtHolderValue, CirValue_ofVar(@CirQ_varid(CirCode_appendNewStmt)), args, 1);

        StmtInfo_insertItem(stmtTable, stmtTableSize, inStmtId, stmtHolderId);
    }

    // Generate args var and args ptr var (for calls)
    CirVarId argsVarId = 0, argsPtrVarId = 0;
    const CirValue *argsVarValue = NULL, *argsPtrVarValue = NULL, *argsPtrVarMem = NULL;
    if (maxNumArgs) {
        argsVarId = CirVar_new(outCodeId);
        argsVarValue = CirValue_ofVar(argsVarId);
        CirVar_setType(argsVarId, CirType_arrayWithLen(cirValueType, maxNumArgs));
        argsPtrVarId = CirVar_new(outCodeId);
        argsPtrVarValue = CirValue_ofVar(argsPtrVarId);
        CirVar_setType(argsPtrVarId, CirType_ptr(cirValueType));
        argsPtrVarMem = CirValue_ofMem(argsPtrVarId);
    }

    // Now lift stmts
    for (inStmtId = CirCode_getFirstStmt(tplCodeId); inStmtId; inStmtId = CirStmt_getNext(inStmtId)) {
        CirVarId stmtHolderId = StmtInfo_lookupStmtHolderId(stmtTable, stmtTableSize, inStmtId);
        const CirValue *stmtHolderValue = CirValue_ofVar(stmtHolderId);

        if (CirStmt_isNop(inStmtId)) {
            // Do nothing
        } else if (CirStmt_isUnOp(inStmtId)) {
            // Gen: CirStmt_toUnOp([stmtHolderValue], lift(dst), opValue, lift(operand1))
            args[0] = stmtHolderValue;
            args[1] = CirQ_liftValue(outCodeId, CirStmt_getDst(inStmtId), &state);
            args[2] = CirValue_ofU64(CIR_IUINT, CirStmt_getOp(inStmtId));
            args[3] = CirQ_liftValue(outCodeId, CirStmt_getOperand1(inStmtId), &state);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toUnOp)), args, 4);
        } else if (CirStmt_isBinOp(inStmtId)) {
            // Gen: CirStmt_toBinOp([stmtHolderValue], lift(dst), opValue, lift(operand1), lift(operand2))
            args[0] = stmtHolderValue;
            args[1] = CirQ_liftValue(outCodeId, CirStmt_getDst(inStmtId), &state);
            args[2] = CirValue_ofU64(CIR_IUINT, CirStmt_getOp(inStmtId));
            args[3] = CirQ_liftValue(outCodeId, CirStmt_getOperand1(inStmtId), &state);
            args[4] = CirQ_liftValue(outCodeId, CirStmt_getOperand2(inStmtId), &state);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toBinOp)), args, 5);
        } else if (CirStmt_isCall(inStmtId)) {
            size_t numArgs = CirStmt_getNumArgs(inStmtId);

            if (numArgs) {
                // Gen: [argsPtrVar] = [argsVar]
                outStmtId = CirCode_appendNewStmt(outCodeId);
                CirStmt_toUnOp(outStmtId, argsPtrVarValue, CIR_UNOP_IDENTITY, argsVarValue);
            }

            for (size_t i = 0; i < numArgs; i = i + 1) {
                // Gen: *[argsPtrVar] = lift(arg)
                const CirValue *lifted = CirQ_liftValue(outCodeId, CirStmt_getArg(inStmtId, i), &state);
                outStmtId = CirCode_appendNewStmt(outCodeId);
                CirStmt_toUnOp(outStmtId, argsPtrVarMem, CIR_UNOP_IDENTITY, lifted);

                // Gen: [argsPtrVar] = [argsPtrVar] + 1
                outStmtId = CirCode_appendNewStmt(outCodeId);
                CirStmt_toBinOp(outStmtId, argsPtrVarValue, CIR_BINOP_PLUS, argsPtrVarValue, CirValue_ofU64(CIR_IUINT, 1));
            }

            // Gen: CirStmt_toCall([stmtHolderValue], lift(dst), lift(target),
            // [argsPtrVar], numArgs)
            args[0] = stmtHolderValue;
            args[1] = CirQ_liftValue(outCodeId, CirStmt_getDst(inStmtId), &state);
            args[2] = CirQ_liftValue(outCodeId, CirStmt_getOperand1(inStmtId), &state);
            if (numArgs)
                args[3] = argsVarValue;
            else
                args[3] = CirValue_ofU64(CIR_IUINT, 0);
            args[4] = CirValue_ofU64(CIR_IUINT, numArgs);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toCall)), args, 5);
        } else if (CirStmt_isReturn(inStmtId)) {
            // Gen: CirStmt_toReturn([stmtHolderValue], lift(op1))
            args[0] = stmtHolderValue;
            args[1] = CirQ_liftValue(outCodeId, CirStmt_getOperand1(inStmtId), &state);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toReturn)), args, 2);
        } else if (CirStmt_isCmp(inStmtId)) {
            // Gen: CirStmt_toCmp([stmtHolderValue], condop, lift(op1),
            // lift(op2), lift(jumpTarget))
            args[0] = stmtHolderValue;
            args[1] = CirValue_ofU64(CIR_IUINT, CirStmt_getOp(inStmtId));
            args[2] = CirQ_liftValue(outCodeId, CirStmt_getOperand1(inStmtId), &state);
            args[3] = CirQ_liftValue(outCodeId, CirStmt_getOperand2(inStmtId), &state);
            args[4] = CirQ_liftStmtId(outCodeId, CirStmt_getJumpTarget(inStmtId), &state);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toCmp)), args, 5);
        } else if (CirStmt_isGoto(inStmtId)) {
            // Gen: CirStmt_toGoto([stmtHolderValue], lift(jumpTarget)]
            args[0] = stmtHolderValue;
            args[1] = CirQ_liftStmtId(outCodeId, CirStmt_getJumpTarget(inStmtId), &state);
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toGoto)), args, 2);
        } else if (CirStmt_isBreak(inStmtId)) {
            // Gen: CirStmt_toBreak([stmtHolderValue])
            args[0] = stmtHolderValue;
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toBreak)), args, 1);
        } else if (CirStmt_isContinue(inStmtId)) {
            // Gen: CirStmt_toContinue([stmtHolderValue])
            args[0] = stmtHolderValue;
            outStmtId = CirCode_appendNewStmt(outCodeId);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirStmt_toContinue)), args, 1);
        } else {
            cir_fatal("unhandled stmt type");
        }
    }

    const CirValue *outValue = CirCode_getValue(tplCodeId);
    if (outValue) {
        // Gen: CirCode_setValue([codeVarValue], lift(outValue))
        args[0] = codeVarValue;
        args[1] = CirQ_liftValue(outCodeId, outValue, &state);
        outStmtId = CirCode_appendNewStmt(outCodeId);
        CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_setValue)), args, 2);
    }

    free(varTable);
    free(stmtTable);
    return outCodeId;
}

#endif // CIRQ_H
