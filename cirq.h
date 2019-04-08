#ifndef CIRQ_H
#define CIRQ_H
#include <stdlib.h>
#include "cir.h"
#include "cirm.h"

static unsigned CirQ_userValueId;
static unsigned CirQ_userStmtId;

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

typedef struct CirAQCodeInfo {
    CirCodeId key;
    CirVarId codeValueHolderId;
} CirAQCodeInfo;

static void
CirAQCodeInfo_insertItem(CirAQCodeInfo *table, size_t size, CirCodeId key, CirVarId codeValueHolderId)
{
    size_t i;
    for (i = key % size; table[i].key; i = (i + 1) % size);
    table[i].key = key;
    table[i].codeValueHolderId = codeValueHolderId;
}

static CirVarId
CirAQCodeInfo_lookupCodeValueHolderId(CirAQCodeInfo *table, size_t size, CirCodeId key)
{
    for (size_t i = key % size; table[i].key; i = (i + 1) % size) {
        if (table[i].key == key)
            return table[i].codeValueHolderId;
    }
    cir_bug("could not find codeValueHolderId for code");
}

typedef struct CirAQInfo {
    CirCodeId codeId;
    const CirValue *value;
    const CirType *type;
} CirAQInfo;

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
    CirAQCodeInfo *aqTable;
    size_t aqTableSize;
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

static CirCodeId
CirQ_type(CirCodeId code_id)
{
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("CirQ_type: no value");
    const CirType *type = CirValue_getTypeValue(value);
    return CirCode_ofExpr(CirValue_ofU64(CIR_IULONG, (uint64_t)type));
}

static const CirValue *
CirQ_liftName(CirCodeId outCodeId, CirName name)
{
    int blah;
    return CirValue_withCastType(CirValue_ofU64(CIR_IULONG, (uint64_t)name), @CirQ_type(__typeval(CirName)));
}

static const CirValue *
CirQ_liftValue(CirCodeId outCodeId, const CirValue *inValue, CirQState *state)
{
    CirStmtId outStmtId;
    const CirValue *args[2];

    if (!inValue)
        return CirValue_ofU64(CIR_IULONG, 0);

    if (CirValue_isLval(inValue)) {
        CirVarId origVarId = CirValue_getVar(inValue);
        CirVarId newVarId = VarIdToVarId_lookup(state->varTable, state->varTableSize, origVarId);
        if (newVarId) {
            CirVarId tmpValueId = CirVar_new(outCodeId);
            CirVar_setType(tmpValueId, state->cirValueType);

            // Gen: [tmpValue] = CirValue_withVar(inValue, [newVarId])
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = CirValue_withCastType(CirValue_ofU64(CIR_IULONG, (uint64_t)inValue), @CirQ_type(__typeval(const CirValue *)));
            args[1] = CirValue_ofVar(newVarId);
            CirStmt_toCall(outStmtId, CirValue_ofVar(tmpValueId), CirValue_ofVar(@CirQ_varid(CirValue_withVar)), args, 2);

            return CirValue_ofVar(tmpValueId);
        }

        return CirValue_withCastType(CirValue_ofU64(CIR_IULONG, (uint64_t)inValue), @CirQ_type(__typeval(const CirValue *)));
    } else if (CirValue_isUser(inValue) == CirQ_userValueId) {
        CirAQInfo *userMem = CirValue_getPtr(inValue);
        CirCodeId embedCodeId = userMem->codeId;
        const CirType *type = userMem->type;
        uint32_t ikind;
        CirVarId tmpVarId;
        const CirValue *tmpVarValue;

        if (CirType_equals(type, @CirQ_type(__typeval(CirCodeId)))) {
            tmpVarId = CirAQCodeInfo_lookupCodeValueHolderId(state->aqTable, state->aqTableSize, embedCodeId);
            return CirValue_ofVar(tmpVarId);
        } else if (CirType_equals(type, @CirQ_type(__typeval(CirValue *)))) {
            // [userMem->value] (type: CirValue *)
            return userMem->value;
        } else if ((ikind = CirType_isInt(type))) {
            tmpVarId = CirVar_new(outCodeId);
            CirVar_setType(tmpVarId, @CirQ_type(__typeval(const CirValue *)));
            tmpVarValue = CirValue_ofVar(tmpVarId);

            // Gen: CirValue_ofU64(type, [userMem->value])
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = CirValue_ofU64(CIR_IUINT, ikind);
            args[1] = userMem->value;
            CirStmt_toCall(outStmtId, tmpVarValue, CirValue_ofVar(@CirQ_varid(CirValue_ofU64)), args, 2);

            // [tmpVar] (type: const CirValue *)
            return tmpVarValue;
        } else {
            CirLog_begin(CIRLOG_FATAL);
            CirLog_print("embedded code value has unsupported type: ");
            CirType_log(type, "");
            CirLog_end();
            cir_fatal("fatal error");
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

    // Some well-known types
    const CirType *cirCodeType = @CirQ_type(__typeval(CirCodeId));
    const CirType *cirValueType = @CirQ_type(__typeval(const CirValue *));
    state.cirValueType = cirValueType;
    const CirType *cirStmtType = @CirQ_type(__typeval(CirStmtId));
    const CirType *cirVarType = @CirQ_type(__typeval(CirVarId));

    // Gen: [codeVarValue] = CirCode_ofExpr(NULL)
    CirVarId codeVarId = CirVar_new(outCodeId);
    CirVar_setType(codeVarId, cirCodeType);
    const CirValue *codeVarValue = CirValue_ofVar(codeVarId);
    outStmtId = CirCode_appendNewStmt(outCodeId);
    args[0] = CirValue_withCastType(CirValue_ofU64(CIR_IINT, 0), @CirQ_type(__typeval(void *)));
    CirStmt_toCall(outStmtId, codeVarValue, CirValue_ofVar(@CirQ_varid(CirCode_ofExpr)), args, 1);

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

        // Gen: CirVar_setName([newVarHolder], name)
        CirName origVarName = CirVar_getName(origVarId);
        if (origVarName) {
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = newVarHolderValue;
            args[1] = CirQ_liftName(outCodeId, origVarName);
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirVar_setName)), args, 2);
        }

        // Gen: CirVar_setType([newVarHolder], type)
        const CirType *origVarType = CirVar_getType(origVarId);
        if (origVarType) {
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = newVarHolderValue;
            args[1] = CirQ_liftType(outCodeId, CirVar_getType(origVarId));
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirVar_setType)), args, 2);
        }

        VarIdToVarId_insertItem(varTable, varTableSize, origVarId, newVarHolderId);
    }

    // Compute the total number of stmts, number of CirAQ codes, and the max number of arguments
    size_t numStmts = 0;
    size_t numAQStmts = 0;
    size_t maxNumArgs = 0;
    for (inStmtId = CirCode_getFirstStmt(tplCodeId); inStmtId; inStmtId = CirStmt_getNext(inStmtId)) {
        if (CirStmt_isCall(inStmtId)) {
            size_t numArgs = CirStmt_getNumArgs(inStmtId);
            if (numArgs > maxNumArgs)
                maxNumArgs = numArgs;
        } else if (CirStmt_isUser(inStmtId) == CirQ_userStmtId) {
            numAQStmts = numAQStmts + 1;
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

    size_t aqTableSize = CirPrime_ge(numAQStmts * 2);
    CirAQCodeInfo *aqTable = calloc(aqTableSize, sizeof(CirAQCodeInfo));
    if (!aqTable)
        cir_fatal("out of memory");
    state.aqTable = aqTable;
    state.aqTableSize = aqTableSize;

    for (inStmtId = CirCode_getFirstStmt(tplCodeId); inStmtId; inStmtId = CirStmt_getNext(inStmtId)) {
        if (CirStmt_isUser(inStmtId) == CirQ_userStmtId) {
            CirAQInfo *userMem = CirStmt_getPtr(inStmtId);
            CirCodeId embedCodeId = userMem->codeId;
            const CirType *type = userMem->type;
            if (CirType_equals(type, @CirQ_type(__typeval(CirCodeId)))) {
                CirVarId valueHolderVar = CirVar_new(outCodeId);
                CirVar_setType(valueHolderVar, @CirQ_type(__typeval(const CirValue *)));
                CirAQCodeInfo_insertItem(aqTable, aqTableSize, embedCodeId, valueHolderVar);
            }
        }

        CirVarId stmtHolderId = CirVar_new(outCodeId);
        CirVar_setType(stmtHolderId, cirStmtType);
        const CirValue *stmtHolderValue = CirValue_ofVar(stmtHolderId);

        // Gen: [stmtHolderValue] = CirStmt_newOrphan()
        outStmtId = CirCode_appendNewStmt(outCodeId);
        CirStmt_toCall(outStmtId, stmtHolderValue, CirValue_ofVar(@CirQ_varid(CirStmt_newOrphan)), NULL, 0);

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
        // Handle CirAQ stmts specially
        if (CirStmt_isUser(inStmtId) == CirQ_userStmtId) {
            CirAQInfo *userMem = CirStmt_getPtr(inStmtId);
            CirCodeId embedCodeId = userMem->codeId;
            const CirType *type = userMem->type;

            // Append our orphan statement first
            CirVarId stmtHolderId = StmtInfo_lookupStmtHolderId(stmtTable, stmtTableSize, inStmtId);
            const CirValue *stmtHolderValue = CirValue_ofVar(stmtHolderId);
            // Gen: CirCode_appendOrphanStmt([codeVarValue], [stmtHolderValue])
            outStmtId = CirCode_appendNewStmt(outCodeId);
            args[0] = codeVarValue;
            args[1] = stmtHolderValue;
            CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_appendOrphanStmt)), args, 2);

            // Resolve any embedded varids which reference vars which we (the
            // template) owns.
            // To do that, we will need to examine all constant CirValues.
            for (CirStmtId localStmtId = CirCode_getFirstStmt(embedCodeId); localStmtId; localStmtId = CirStmt_getNext(localStmtId)) {
                size_t numOperands = CirStmt_getNumOperands(localStmtId);
                for (size_t i = 0; i < numOperands; i = i + 1) {
                    const CirValue *value = CirStmt_getOperand(localStmtId, i);
                    if (!value)
                        continue;
                    if (!CirValue_isInt(value))
                        continue;
                    const CirType *valueType = CirValue_getType(value);
                    if (!valueType)
                        continue;
                    valueType = CirType_lvalConv(valueType);
                    if (CirType_equals(valueType, @CirQ_type(__typeval(CirValue *)))) {
                        const CirValue *constValue = (const CirValue *)CirValue_getU64(value);
                        if (!constValue)
                            continue;
                        if (!CirValue_isLval(constValue))
                            continue;
                        CirVarId origVarId = CirValue_getVar(constValue);
                        CirVarId newVarId = VarIdToVarId_lookup(varTable, varTableSize, origVarId);
                        if (!newVarId)
                            continue;

                        CirVarId tmpVarId = CirVar_new(outCodeId);
                        const CirValue *tmpVarValue = CirValue_ofVar(tmpVarId);
                        CirVar_setType(tmpVarId, @CirQ_type(__typeval(const CirValue *)));

                        // Gen: [tmpVar] = CirValue_withVar([constValue], [newVarId])
                        outStmtId = CirCode_appendNewStmt(outCodeId);
                        args[0] = value;
                        args[1] = CirValue_ofVar(newVarId);
                        CirStmt_toCall(outStmtId, tmpVarValue, CirValue_ofVar(@CirQ_varid(CirValue_withVar)), args, 2);

                        CirStmt_setOperand(localStmtId, i, tmpVarValue);
                    } else if (CirType_equals(valueType, @CirQ_type(__typeval(CirVarId)))) {
                        cir_bug("TODO");
                    }
                }
            }

            if (CirType_equals(type, @CirQ_type(__typeval(CirCodeId)))) {
                CirCode_append(outCodeId, embedCodeId);

                // Gen: [valueHolderVar] = CirCode_getValue([embedCodeValue])
                CirVarId valueHolderVar = CirAQCodeInfo_lookupCodeValueHolderId(aqTable, aqTableSize, embedCodeId);
                args[0] = userMem->value;
                outStmtId = CirCode_appendNewStmt(outCodeId);
                CirStmt_toCall(outStmtId, CirValue_ofVar(valueHolderVar), CirValue_ofVar(@CirQ_varid(CirCode_getValue)), args, 1);

                // Gen: CirCode_append([codeVarValue], [embedCodeValue])
                args[0] = codeVarValue;
                args[1] = userMem->value;
                outStmtId = CirCode_appendNewStmt(outCodeId);
                CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_append)), args, 2);
            } else {
                CirCode_append(outCodeId, embedCodeId);
            }
            continue;
        }

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
        } else {
            cir_fatal("unhandled stmt type");
        }

        // Gen: CirCode_appendOrphanStmt([codeVarValue], [stmtHolderValue])
        outStmtId = CirCode_appendNewStmt(outCodeId);
        args[0] = codeVarValue;
        args[1] = stmtHolderValue;
        CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_appendOrphanStmt)), args, 2);
    }

    const CirValue *outValue = CirCode_getValue(tplCodeId);
    if (outValue) {
        // Gen: CirCode_setValue([codeVarValue], lift(outValue))
        args[0] = codeVarValue;
        args[1] = CirQ_liftValue(outCodeId, outValue, &state);
        outStmtId = CirCode_appendNewStmt(outCodeId);
        CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_setValue)), args, 2);
    }

    // Gen: CirCode_typecheck([codeVarValue], NULL)
    args[0] = codeVarValue;
    args[1] = CirValue_ofU64(CIR_IINT, 0);
    outStmtId = CirCode_appendNewStmt(outCodeId);
    CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_typecheck)), args, 2);

    free(varTable);
    free(stmtTable);
    free(aqTable);
    CirCode_setValue(outCodeId, codeVarValue);
    CirCode_typecheck(outCodeId, NULL);
    return outCodeId;
}

static CirCodeId CirQ_append(CirCodeId targetCodeId, CirCodeId tplCodeId) {
    const CirValue *targetCodeValue = CirCode_getValue(targetCodeId);
    if (!targetCodeId)
        cir_fatal("CirQ_append: lhs must be a target code id");
    CirCodeId outCodeId = CirQ(tplCodeId);
    const CirValue *computedCodeValue = CirCode_getValue(outCodeId);
    CirStmtId outStmtId = CirCode_appendNewStmt(outCodeId);
    const CirValue *args[2];
    args[0] = targetCodeValue;
    args[1] = computedCodeValue;
    CirStmt_toCall(outStmtId, NULL, CirValue_ofVar(@CirQ_varid(CirCode_append)), args, 2);
    CirCode_setValue(outCodeId, NULL);
    return outCodeId;
}

static CirCodeId CirAQ(CirCodeId codeId) {
    if (!CirCode_getValue(codeId))
        cir_fatal("CirAQ: code must have a value");
    CirAQInfo *userMem = CirMem_balloc(sizeof(CirAQInfo), _Alignof(CirAQInfo));
    userMem->codeId = codeId;
    const CirValue *embedCodeValue = CirCode_getValue(codeId);
    if (!embedCodeValue)
        cir_fatal("embedded code has no value");
    userMem->value = embedCodeValue;
    const CirType *type = CirValue_getType(embedCodeValue);
    if (!type)
        cir_fatal("embedded code value has unknown type");
    userMem->type = type;

    CirCodeId outCodeId = CirCode_ofExpr(CirValue_ofUser(CirQ_userValueId, userMem));

    // Stmt used to prompt CirQ to inline the code
    CirStmtId outStmtId = CirCode_appendNewStmt(outCodeId);
    CirStmt_toUser(outStmtId, CirQ_userStmtId, userMem);

    return outCodeId;
}

static void CirQ_init(void) {
    CirQ_userValueId = CirValue_registerUser();
    CirQ_userStmtId = CirStmt_registerUser();
}

@CirQ_init();

#endif // CIRQ_H
