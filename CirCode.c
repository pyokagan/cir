#include "cir_internal.h"
#include <assert.h>
#include <stdlib.h>

#define MAX_CODE (1024 * 1024 * 1)

// data1:
// bits 31 to 29: type
// bit 28: invalidation bit

#define CIRCODE_EXPR 1
#define CIRCODE_COND 2

#define data1ToType(x) (((x) >> 29) & 0x07)
#define typeToData1(x) (((x) & 0x07) << 29)
#define data1ClearType(x) ((x) & (~(0x07 << 29)))

typedef CirArray(CirStmtId) CirStmtIdArray;

typedef struct CirCode {
    uint32_t data1;
    CirStmtId firstStmt;
    CirStmtId lastStmt;
    const CirValue *value;
    CirArray(CirVarId) vars;
    CirStmtIdArray truejumps; // or nextjumps
    CirStmtIdArray falsejumps;
} CirCode;

static CirCode codes[MAX_CODE];
static uint32_t numCodes = 1;

static void
backpatch(CirStmtIdArray *arr, CirStmtId stmt_id)
{
    for (size_t i = 0; i < arr->len; i++)
        CirStmt_setJumpTarget(arr->items[i], stmt_id);
    arr->len = 0;
}

static void
CirCode__setType(CirCodeId code_id, uint32_t type)
{
    codes[code_id].data1 = data1ClearType(codes[code_id].data1) | typeToData1(type);
}

void
CirCode__setLastStmt(CirCodeId code_id, CirStmtId stmt_id)
{
    codes[code_id].lastStmt = stmt_id;
}

void
CirCode__setFirstStmt(CirCodeId code_id, CirStmtId stmt_id)
{
    codes[code_id].firstStmt = stmt_id;
}

CirCodeId
CirCode_ofExpr(const CirValue *value)
{
    if (numCodes >= MAX_CODE)
        cir_bug("CirCode_ofExpr: too many CirCodes");

    CirCodeId code_id = numCodes++;
    codes[code_id].data1 = typeToData1(CIRCODE_EXPR);
    codes[code_id].firstStmt = 0;
    codes[code_id].lastStmt = 0;
    codes[code_id].value = value;
    return code_id;
}

CirCodeId
CirCode_ofCond(void)
{
    if (numCodes >= MAX_CODE)
        cir_bug("CirCode_ofCond: too many CirCodes");

    CirCodeId code_id = numCodes++;
    codes[code_id].data1 = typeToData1(CIRCODE_COND);
    codes[code_id].firstStmt = 0;
    codes[code_id].lastStmt = 0;
    codes[code_id].value = NULL;
    return code_id;
}

void
CirCode_free(CirCodeId code_id)
{
    if (!code_id)
        return;
    codes[code_id].data1 |= 0x10000000;
    CirArray_release(&codes[code_id].vars);
    CirArray_release(&codes[code_id].truejumps);
    CirArray_release(&codes[code_id].falsejumps);
}

static bool
CirCode_isFreed(CirCodeId code_id)
{
    return codes[code_id].data1 & 0x10000000;
}

void
CirCode__addVar(CirCodeId code_id, CirVarId var_id)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));
    CirArray_push(&codes[code_id].vars, &var_id);
}

void
CirCode_addTrueJump(CirCodeId code_id, CirStmtId stmt_id)
{
    assert(CirCode_isCond(code_id));
    CirArray_push(&codes[code_id].truejumps, &stmt_id);
}

void
CirCode_addFalseJump(CirCodeId code_id, CirStmtId stmt_id)
{
    assert(CirCode_isCond(code_id));
    CirArray_push(&codes[code_id].falsejumps, &stmt_id);
}

CirStmtId
CirCode_appendNewStmt(CirCodeId code_id)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));
    CirStmtId last_stmt_id = CirCode_getLastStmt(code_id);
    if (last_stmt_id) {
        return CirStmt_newAfter(last_stmt_id);
    } else {
        last_stmt_id = CirStmt__new(code_id);
        CirCode__setFirstStmt(code_id, last_stmt_id);
        CirCode__setLastStmt(code_id, last_stmt_id);
        return last_stmt_id;
    }
}

bool
CirCode_isExpr(CirCodeId code_id)
{
    assert(!CirCode_isFreed(code_id));
    return data1ToType(codes[code_id].data1) == CIRCODE_EXPR;
}

bool
CirCode_isCond(CirCodeId code_id)
{
    assert(!CirCode_isFreed(code_id));
    return data1ToType(codes[code_id].data1) == CIRCODE_COND;
}

const CirValue *
CirCode_getValue(CirCodeId code_id)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));
    if (data1ToType(codes[code_id].data1) != CIRCODE_EXPR)
        cir_bug("CirCode_getValue: not a expr code");
    return codes[code_id].value;
}

void
CirCode_setValue(CirCodeId code_id, const CirValue *value)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));
    if (data1ToType(codes[code_id].data1) != CIRCODE_EXPR)
        cir_bug("CirCode_setValue: not a expr code");
    codes[code_id].value = value;
}

CirStmtId
CirCode_getFirstStmt(CirCodeId code_id)
{
    assert(!CirCode_isFreed(code_id));
    CirStmtId stmt_id = codes[code_id].firstStmt;
    assert(!stmt_id || !CirStmt_getPrev(stmt_id));
    assert(stmt_id || !codes[code_id].lastStmt);
    return stmt_id;
}

CirStmtId
CirCode_getLastStmt(CirCodeId code_id)
{
    assert(!CirCode_isFreed(code_id));
    CirStmtId stmt_id = codes[code_id].lastStmt;
    assert(!stmt_id || !CirStmt_getNext(stmt_id));
    assert(stmt_id || !codes[code_id].firstStmt);
    return stmt_id;
}

static void
appendCode(CirCodeId dst, CirCodeId src)
{
    assert(!CirCode_isFreed(dst));
    assert(!CirCode_isFreed(src));

    // Join the code together
    CirStmtId srcFirstStmt = CirCode_getFirstStmt(src);
    if (srcFirstStmt) {
        // Transfer ownership of src's code to dst
        CirStmtId srcLastStmt = CirCode_getLastStmt(src);
        CirStmt__setNextCode(srcLastStmt, dst);
        CirStmt__setPrevCode(srcFirstStmt, dst);

        CirStmtId dstLastStmt = CirCode_getLastStmt(dst);
        if (dstLastStmt) {
            CirStmt__setNextStmt(dstLastStmt, srcFirstStmt);
            CirStmt__setPrevStmt(srcFirstStmt, dstLastStmt);
        } else {
            codes[dst].firstStmt = srcFirstStmt;
        }

        codes[dst].lastStmt = srcLastStmt;
    }
}

static void
appendVars(CirCodeId dst, CirCodeId src)
{
    assert(!CirCode_isFreed(dst));
    assert(!CirCode_isFreed(src));

    // Join vars together
    CirArray_grow(&codes[dst].vars, codes[src].vars.len);
    for (size_t i = 0; i < codes[src].vars.len; i++) {
        CirVarId v = codes[src].vars.items[i];
        assert(CirVar_getOwner(v) == src);
        codes[dst].vars.items[codes[dst].vars.len + i] = v;
        CirVar__setOwner(v, dst);
    }
    codes[dst].vars.len += codes[src].vars.len;
}

CirCodeId
CirCode_toExpr(CirCodeId code_id, bool dropValue)
{
    if (CirCode_isExpr(code_id)) {
        if (dropValue)
            CirCode_setValue(code_id, NULL);
        return code_id;
    } else if (CirCode_isCond(code_id)) {
        if (dropValue) {
            // Only create a dummy statement if needed
            if (codes[code_id].truejumps.len || codes[code_id].falsejumps.len) {
                CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
                backpatch(&codes[code_id].truejumps, stmt_id);
                backpatch(&codes[code_id].falsejumps, stmt_id);
            }
            // Convert to expr code
            CirCode__setType(code_id, CIRCODE_EXPR);
            CirCode_setValue(code_id, NULL);
            return code_id;
        }

        // In-place conversion
        if (codes[code_id].truejumps.len > 0 && codes[code_id].falsejumps.len > 0) {
            CirStmtId stmt_id, jump_id;

            // True and false jumps
            CirVarId result_id = CirVar_new(code_id);
            CirVar_setType(result_id, CirType_int(CIR_IINT));
            const CirValue *result_val = CirValue_ofVar(result_id);

            // Truejump target
            stmt_id = CirCode_appendNewStmt(code_id);
            CirStmt_toUnOp(stmt_id, result_val, CIR_UNOP_IDENTITY, CirValue_ofI64(CIR_IINT, 1));
            backpatch(&codes[code_id].truejumps, stmt_id);
            stmt_id = CirCode_appendNewStmt(code_id);
            CirStmt_toGoto(stmt_id, 0);
            jump_id = stmt_id;

            // Falsejump target
            stmt_id = CirCode_appendNewStmt(code_id);
            CirStmt_toUnOp(stmt_id, result_val, CIR_UNOP_IDENTITY, CirValue_ofI64(CIR_IINT, 0));
            backpatch(&codes[code_id].falsejumps, stmt_id);

            // Rest target
            stmt_id = CirCode_appendNewStmt(code_id);
            CirStmt_setJumpTarget(jump_id, stmt_id);

            // Convert to expr code
            CirCode__setType(code_id, CIRCODE_EXPR);
            CirCode_setValue(code_id, result_val);
        } else if (codes[code_id].truejumps.len > 0) {
            // Always true.
            CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
            backpatch(&codes[code_id].truejumps, stmt_id);
            // Constant is 1
            CirCode__setType(code_id, CIRCODE_EXPR);
            CirCode_setValue(code_id, CirValue_ofI64(CIR_IINT, 1));
        } else if (codes[code_id].falsejumps.len > 0) {
            // Always false.
            CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
            backpatch(&codes[code_id].truejumps, stmt_id);
            // Constant is 0
            CirCode__setType(code_id, CIRCODE_EXPR);
            CirCode_setValue(code_id, CirValue_ofI64(CIR_IINT, 0));
        } else {
            cir_bug("CirCode cond without truejumps/falsejumps!?");
        }
        return code_id;
    } else {
        cir_bug("CirCode_toExpr: unhandled case");
    }
}

static void
CirCode__toEmptyCond(CirCodeId code_id)
{
    assert(CirCode_isExpr(code_id));
    CirCode__setType(code_id, CIRCODE_COND);
    codes[code_id].truejumps.len = 0;
    codes[code_id].falsejumps.len = 0;
}

const CirType *
CirCode_getType(CirCodeId code_id)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));

    if (CirCode_isExpr(code_id)) {
        const CirValue *value = CirCode_getValue(code_id);
        return value ? CirValue_getType(value) : CirType_void();
    } else if (CirCode_isCond(code_id)) {
        return CirType_int(CIR_IINT);
    } else {
        cir_bug("unreachable");
    }
}

void
CirCode_append(CirCodeId dst, CirCodeId src)
{
    if (!src)
        return;

    appendCode(dst, src);
    appendVars(dst, src);

    if (CirCode_isExpr(dst)) {
        if (CirCode_isExpr(src)) {
            codes[dst].value = codes[src].value;
        } else if (CirCode_isCond(src)) {
            CirCode__setType(dst, CIRCODE_COND);
            // swap truejumps and falsejumps
            {
                CirStmtIdArray tmp = codes[dst].truejumps;
                codes[dst].truejumps = codes[src].truejumps;
                codes[src].truejumps = tmp;
            }
            {
                CirStmtIdArray tmp = codes[dst].falsejumps;
                codes[dst].falsejumps = codes[src].falsejumps;
                codes[src].falsejumps = tmp;
            }
        } else {
            cir_fatal("unreachable");
        }
    } else if (CirCode_isCond(dst)) {
        if (CirCode_isExpr(src)) {
            // No need to do anything
        } else if (CirCode_isCond(src)) {
            // Append truejumps, falsejumps
            CirArray_grow(&codes[dst].truejumps, codes[src].truejumps.len);
            memcpy(codes[dst].truejumps.items + codes[dst].truejumps.len, codes[src].truejumps.items, sizeof(CirStmtId) * codes[src].truejumps.len);
            codes[dst].truejumps.len += codes[src].truejumps.len;
            CirArray_grow(&codes[dst].falsejumps, codes[src].falsejumps.len);
            memcpy(codes[dst].falsejumps.items + codes[dst].falsejumps.len, codes[src].falsejumps.items, sizeof(CirStmtId) * codes[src].falsejumps.len);
            codes[dst].falsejumps.len += codes[src].falsejumps.len;
        } else {
            cir_fatal("unreachable");
        }
    } else {
        cir_fatal("incompatible");
    }

    CirCode_free(src);
}

void
CirCode_dump(CirCodeId cid)
{
    if (cid == 0) {
        CirLog_print("<CirCode 0>\n");
        return;
    }

    // Starting marker
    CirLog_printf("/* cid%u start */\n", (unsigned)cid);

    // Log vars
    for (size_t i = 0; i < codes[cid].vars.len; i++) {
        CirVar_logNameAndType(codes[cid].vars.items[i]);
        CirLog_print(";\n");
    }

    // Log stmts
    CirStmtId stmt_id = CirCode_getFirstStmt(cid);
    while (stmt_id) {
        CirStmt_log(stmt_id);
        CirLog_print("\n");
        stmt_id = CirStmt_getNext(stmt_id);
    }

    // Ending marker
    if (CirCode_isExpr(cid)) {
        CirLog_printf("/* cid%u end, value: ", (unsigned)cid);
        CirValue_log(CirCode_getValue(cid));
        CirLog_print(" */\n");
    } else {
        CirLog_printf("/* cid%u end */\n", (unsigned)cid);
    }
}

size_t
CirCode_getNumVars(CirCodeId code_id)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));

    return codes[code_id].vars.len;
}

CirVarId
CirCode_getVar(CirCodeId code_id, size_t i)
{
    assert(code_id != 0);
    assert(!CirCode_isFreed(code_id));
    assert(i < codes[code_id].vars.len);
    return codes[code_id].vars.items[i];
}


static int64_t
truncToIkindS(uint32_t ikind, int64_t val, const CirMachine *mach)
{
    switch (CirIkind_size(ikind, mach)) {
    case 1:
        return (int8_t)val;
    case 2:
        return (int16_t)val;
    case 4:
        return (int32_t)val;
    case 8:
        return val;
    default:
        cir_bug("truncToIkindS fail");
    }
}

static uint64_t
truncToIkindU(uint32_t ikind, uint64_t val, const CirMachine *mach)
{
    switch (CirIkind_size(ikind, mach)) {
    case 1:
        return (uint8_t)val;
    case 2:
        return (uint16_t)val;
    case 4:
        return (uint32_t)val;
    case 8:
        return val;
    default:
        cir_bug("truncToIkindU fail");
    }
}

// Performs arithmetic conversion
#define BINARITH_TEMPLATE(name, runtime_op, comptime_expr) \
    lhs = CirCode_toExpr(lhs, false); \
    rhs = CirCode_toExpr(rhs, false); \
    const CirValue *lhs_value = CirCode_getValue(lhs); \
    const CirValue *rhs_value = CirCode_getValue(rhs); \
    if (!lhs_value) \
        cir_fatal(name ": left operand is void"); \
    if (!rhs_value) \
        cir_fatal(name ": right operand is void"); \
    const CirType *lhs_type = CirValue_getType(lhs_value); \
    const CirType *rhs_type = CirValue_getType(rhs_value); \
    assert(lhs_type); \
    assert(rhs_type); \
    CirCode_append(lhs, rhs); \
    const CirType *dst_type = CirType__arithmeticConversion(lhs_type, rhs_type, mach); \
    if (CirValue_isLval(lhs_value) || CirValue_isLval(rhs_value)) { \
        CirVarId dst_id = CirVar_new(lhs); \
        const CirValue *dst_var = CirValue_ofVar(dst_id); \
        CirVar_setType(dst_id, dst_type); \
        CirStmtId stmt_id = CirCode_appendNewStmt(lhs); \
        CirStmt_toBinOp(stmt_id, dst_var, runtime_op, lhs_value, rhs_value); \
        CirCode_setValue(lhs, dst_var); \
        return lhs; \
    } else { \
        uint32_t ikind = CirType_isInt(dst_type); \
        const CirValue *dst_value; \
        if (ikind) { \
            if (CirIkind_isSigned(ikind, mach)) { \
                int64_t a = CirValue_getI64(lhs_value); \
                int64_t b = CirValue_getI64(rhs_value); \
                int64_t c = comptime_expr; \
                dst_value = CirValue_ofI64(ikind, truncToIkindS(ikind, c, mach)); \
            } else { \
                uint64_t a = CirValue_getU64(lhs_value); \
                uint64_t b = CirValue_getU64(rhs_value); \
                uint64_t c = comptime_expr; \
                dst_value = CirValue_ofU64(ikind, truncToIkindU(ikind, c, mach)); \
            } \
        } else { \
            cir_bug("TODO: cannot handle"); \
        } \
        CirCode_setValue(lhs, dst_value); \
        return lhs; \
    }

#define RELOP_TEMPLATE(name, runtime_op, comptime_expr) \
    lhs = CirCode_toExpr(lhs, false); \
    rhs = CirCode_toExpr(rhs, false); \
    const CirValue *lhs_value = CirCode_getValue(lhs); \
    const CirValue *rhs_value = CirCode_getValue(rhs); \
    if (!lhs_value) \
        cir_fatal(name ": left operand is void"); \
    if (!rhs_value) \
        cir_fatal(name ": right operand is void"); \
    const CirType *lhs_type = CirValue_getType(lhs_value); \
    const CirType *rhs_type = CirValue_getType(rhs_value); \
    assert(lhs_type); \
    assert(rhs_type); \
    CirCode_append(lhs, rhs); \
    const CirType *dst_type = CirType__arithmeticConversion(lhs_type, rhs_type, mach); \
    if (CirValue_isLval(lhs_value) || CirValue_isLval(rhs_value)) { \
        CirCode__toEmptyCond(lhs); \
        CirStmtId stmt_id = CirCode_appendNewStmt(lhs); \
        CirStmt_toCmp(stmt_id, runtime_op, lhs_value, rhs_value, 0); \
        CirCode_addTrueJump(lhs, stmt_id); \
        stmt_id = CirCode_appendNewStmt(lhs); \
        CirStmt_toGoto(stmt_id, 0); \
        CirCode_addFalseJump(lhs, stmt_id); \
        return lhs; \
    } else { \
        uint32_t ikind = CirType_isInt(dst_type); \
        const CirValue *dst_value; \
        if (ikind) { \
            if (CirIkind_isSigned(ikind, mach)) { \
                int64_t a = CirValue_getI64(lhs_value); \
                int64_t b = CirValue_getI64(rhs_value); \
                dst_value = CirValue_ofI64(CIR_IINT, comptime_expr); \
            } else { \
                uint64_t a = CirValue_getU64(lhs_value); \
                uint64_t b = CirValue_getU64(rhs_value); \
                dst_value = CirValue_ofI64(CIR_IINT, comptime_expr); \
            } \
        } else { \
            cir_bug("TODO: cannot handle"); \
        } \
        CirCode_setValue(lhs, dst_value); \
        return lhs; \
    }

CirCodeId
CirBuild__mul(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    BINARITH_TEMPLATE("mul", CIR_BINOP_MUL, a * b)
}

CirCodeId
CirBuild__div(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    BINARITH_TEMPLATE("div", CIR_BINOP_DIV, a / b)
}

CirCodeId
CirBuild__mod(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    BINARITH_TEMPLATE("mod", CIR_BINOP_MOD, a % b);
}

static CirCodeId
CirBuild__plusA(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    BINARITH_TEMPLATE("plusA", CIR_BINOP_PLUS, a + b)
}

static CirCodeId
CirBuild__minusA(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    BINARITH_TEMPLATE("minusA", CIR_BINOP_MINUS, a - b)
}

CirCodeId
CirBuild__simpleAssign(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    rhs = CirCode_toExpr(rhs, false);
    if (!CirCode_isExpr(lhs))
        cir_fatal("simple assign: expected code on lhs");

    const CirValue *rhs_value = CirCode_getValue(rhs);
    if (!rhs_value)
        cir_fatal("simple assign: rhs is void");

    const CirValue *lhs_value = CirCode_getValue(lhs);
    if (!lhs_value)
        cir_fatal("simple assign: lhs is void");

    CirCode_append(rhs, lhs);
    CirStmtId stmt_id = CirCode_appendNewStmt(rhs);
    CirStmt_toUnOp(stmt_id, lhs_value, CIR_UNOP_IDENTITY, rhs_value);
    CirCode_setValue(rhs, lhs_value);
    return rhs;
}

static void
CirBuild__plusPtr(CirCodeId code_id, const CirType *ptrType, const CirValue *lhsValue, const CirValue *rhsValue)
{
    // TODO: Support constant ptrs (e.g. NULL ptr)
    CirVarId dst_id = CirVar_new(code_id);
    const CirValue *dst_var = CirValue_ofVar(dst_id);
    CirVar_setType(dst_id, ptrType);
    CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
    CirStmt_toBinOp(stmt_id, dst_var, CIR_BINOP_PLUS, lhsValue, rhsValue);
    CirCode_setValue(code_id, dst_var);
}

// Handles overloaded +
CirCodeId
CirBuild__plus(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    lhs = CirCode_toExpr(lhs, false);
    rhs = CirCode_toExpr(rhs, false);
    const CirValue *lhs_value = CirCode_getValue(lhs);
    const CirValue *rhs_value = CirCode_getValue(rhs);
    const CirType *lhs_type = CirValue_getType(lhs_value);
    const CirType *rhs_type = CirValue_getType(rhs_value);
    lhs_type = CirType_lvalConv(lhs_type);
    rhs_type = CirType_lvalConv(rhs_type);
    const CirType *lhs_unrolledType = lhs_type ? CirType_unroll(lhs_type) : NULL;
    const CirType *rhs_unrolledType = rhs_type ? CirType_unroll(rhs_type) : NULL;
    if (!lhs_unrolledType || !rhs_unrolledType)
        goto fallback;
    if (CirType_isArithmetic(lhs_unrolledType) && CirType_isArithmetic(rhs_unrolledType)) {
        return CirBuild__plusA(lhs, rhs, mach);
    } else if (CirType_isPtr(lhs_unrolledType) && CirType_isInt(rhs_unrolledType)) {
        CirCode_append(lhs, rhs);
        CirBuild__plusPtr(lhs, lhs_type, lhs_value, rhs_value);
        return lhs;
    } else if (CirType_isInt(lhs_unrolledType) && CirType_isPtr(rhs_unrolledType)) {
        CirCode_append(lhs, rhs);
        CirBuild__plusPtr(lhs, rhs_type, lhs_value, rhs_value);
        return lhs;
    }

fallback:
    CirCode_append(lhs, rhs);
    CirVarId dst_id = CirVar_new(lhs);
    const CirValue *dst_var = CirValue_ofVar(dst_id);
    CirStmtId stmt_id = CirCode_appendNewStmt(lhs);
    CirStmt_toBinOp(stmt_id, dst_var, CIR_BINOP_PLUS, lhs_value, rhs_value);
    CirCode_setValue(lhs, dst_var);
    return lhs;
}

CirCodeId
CirBuild__arraySubscript(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    CirCodeId code_id = CirBuild__plus(lhs, rhs, mach);
    const CirValue *value = CirCode_getValue(code_id);
    assert(value);
    assert(CirValue_isVar(value));
    assert(!CirValue_getNumFields(value));
    CirVarId var_id = CirValue_getVar(value);
    CirCode_setValue(code_id, CirValue_ofMem(var_id));
    return code_id;
}

static void
CirBuild__minusPtr(CirCodeId code_id, const CirType *ptrType, const CirValue *lhsValue, const CirValue *rhsValue)
{
    // TODO: Support constant ptrs (e.g. NULL ptr)
    CirVarId dst_id = CirVar_new(code_id);
    const CirValue *dst_var = CirValue_ofVar(dst_id);
    CirVar_setType(dst_id, ptrType);
    CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
    CirStmt_toBinOp(stmt_id, dst_var, CIR_BINOP_MINUS, lhsValue, rhsValue);
    CirCode_setValue(code_id, dst_var);
}

// Handles overloaded -
CirCodeId
CirBuild__minus(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    lhs = CirCode_toExpr(lhs, false);
    rhs = CirCode_toExpr(rhs, false);
    const CirValue *lhs_value = CirCode_getValue(lhs);
    const CirValue *rhs_value = CirCode_getValue(rhs);
    const CirType *lhs_type = CirValue_getType(lhs_value);
    const CirType *rhs_type = CirValue_getType(rhs_value);
    if (CirType_isArithmetic(lhs_type) && CirType_isArithmetic(rhs_type)) {
        return CirBuild__minusA(lhs, rhs, mach);
    } else if (CirType_isPtr(lhs_type) && CirType_isInt(rhs_type)) {
        CirCode_append(lhs, rhs);
        CirBuild__minusPtr(lhs, lhs_type, lhs_value, rhs_value);
        return lhs;
    } else if (CirType_isPtr(lhs_type) && CirType_isPtr(rhs_type)) {
        cir_bug("TODO: MinusPP");
    } else {
        cir_fatal("Invalid operands to binary minus operator");
    }
}

CirCodeId
CirBuild__call(CirCodeId target, const CirCodeId *args, size_t numArgs, const CirMachine *mach)
{
    const CirValue *targetValue = CirCode_getValue(target);
    if (!targetValue)
        cir_fatal("CirBuild__call: target has no value");

    // Get the result type
    const CirType *targetType = CirCode_getType(target);
    const CirType *targetTypeUnrolled = CirType_unroll(targetType);
    const CirType *returnType;
    if (CirType_isFun(targetTypeUnrolled)) {
        returnType = CirType_getBaseType(targetTypeUnrolled);
    } else if (CirType_isPtr(targetTypeUnrolled)) {
        const CirType *bt = CirType_getBaseType(targetTypeUnrolled);
        const CirType *btUnrolled = CirType_unroll(bt);
        if (!CirType_isFun(btUnrolled))
            goto target_type_fail;
        returnType = CirType_getBaseType(btUnrolled);
    } else {
target_type_fail:
        CirLog_begin(CIRLOG_FATAL);
        CirLog_print("CirBuild__call: ");
        CirType_log(targetType, NULL);
        CirLog_print(" is not callable");
        CirLog_end();
        exit(1);
    }

    // Evaluate arguments from right-to-left
    CirArray(const CirValue *) argValues = CIRARRAY_INIT;
    CirArray_alloc(&argValues, numArgs);
    CirCodeId argCode = 0;
    for (size_t i = 0; i < numArgs; i++) {
        CirCodeId code_id = args[numArgs - i - 1];
        argValues.items[numArgs - i - 1] = CirCode_getValue(code_id);
        if (!argCode)
            argCode = code_id;
        else
            CirCode_append(argCode, code_id);
    }

    if (!argCode)
        argCode = CirCode_ofExpr(NULL);

    const CirValue *dst;
    if (CirType_isVoid(returnType)) {
        dst = NULL;
        CirCode_setValue(argCode, NULL);
    } else {
        // Create temporary variable for return
        CirVarId result_vid = CirVar_new(argCode);
        CirVar_setType(result_vid, returnType);
        dst = CirValue_ofVar(result_vid);
        CirCode_setValue(argCode, dst);
    }

    CirStmtId callstmt_id = CirCode_appendNewStmt(argCode);
    CirStmt_toCall(callstmt_id, dst, targetValue, argValues.items, numArgs);
    CirArray_release(&argValues);
    return argCode;
}

CirCodeId
CirBuild__lt(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("LT", CIR_CONDOP_LT, a < b)
}

CirCodeId
CirBuild__le(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("LE", CIR_CONDOP_LE, a <= b)
}

CirCodeId
CirBuild__gt(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("GT", CIR_CONDOP_GT, a > b)
}

CirCodeId
CirBuild__ge(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("GE", CIR_CONDOP_GE, a >= b)
}

static CirCodeId
CirBuild__eqA(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("EQ_A", CIR_CONDOP_EQ, a == b);
}

CirCodeId
CirBuild__eq(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    lhs = CirCode_toExpr(lhs, false);
    rhs = CirCode_toExpr(rhs, false);
    const CirValue *lhs_value = CirCode_getValue(lhs);
    if (!lhs_value)
        cir_fatal("eq: lhs has no value");
    const CirValue *rhs_value = CirCode_getValue(rhs);
    if (!rhs_value)
        cir_fatal("eq: rhs has no value");
    const CirType *lhs_type = CirValue_getType(lhs_value);
    const CirType *rhs_type = CirValue_getType(rhs_value);
    if (lhs_type && rhs_type && CirType_isArithmetic(lhs_type) && CirType_isArithmetic(rhs_type)) {
        return CirBuild__eqA(lhs, rhs, mach);
    } else {
        CirCode_append(lhs, rhs);
        CirCode__toEmptyCond(lhs);
        CirStmtId stmt_id = CirCode_appendNewStmt(lhs);
        CirStmt_toCmp(stmt_id, CIR_CONDOP_EQ, lhs_value, rhs_value, 0);
        CirCode_addTrueJump(lhs, stmt_id);
        stmt_id = CirCode_appendNewStmt(lhs);
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addFalseJump(lhs, stmt_id);
        return lhs;
    }
}

static CirCodeId
CirBuild__neA(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    RELOP_TEMPLATE("NE_A", CIR_CONDOP_NE, a != b);
}

CirCodeId
CirBuild__ne(CirCodeId lhs, CirCodeId rhs, const CirMachine *mach)
{
    lhs = CirCode_toExpr(lhs, false);
    rhs = CirCode_toExpr(rhs, false);
    const CirValue *lhs_value = CirCode_getValue(lhs);
    if (!lhs_value)
        cir_fatal("ne: lhs has no value");
    const CirValue *rhs_value = CirCode_getValue(rhs);
    if (!rhs_value)
        cir_fatal("ne: rhs has no value");
    const CirType *lhs_type = CirValue_getType(lhs_value);
    const CirType *rhs_type = CirValue_getType(rhs_value);
    if (lhs_type && rhs_type && CirType_isArithmetic(lhs_type) && CirType_isArithmetic(rhs_type)) {
        return CirBuild__neA(lhs, rhs, mach);
    } else {
        CirCode_append(lhs, rhs);
        CirCode__toEmptyCond(lhs);
        CirStmtId stmt_id = CirCode_appendNewStmt(lhs);
        CirStmt_toCmp(stmt_id, CIR_CONDOP_NE, lhs_value, rhs_value, 0);
        CirCode_addTrueJump(lhs, stmt_id);
        stmt_id = CirCode_appendNewStmt(lhs);
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addFalseJump(lhs, stmt_id);
        return lhs;
    }
}

static void
toCond(CirCodeId code_id, CirStmtId *firstStmt)
{
    assert(CirCode_isExpr(code_id));
    // Need to generate a runtime cmp
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("toCond: no value");
    CirCode__toEmptyCond(code_id);
    if (CirValue_isInt(value)) {
        // True or false jump only
        CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
        if (firstStmt)
            *firstStmt = stmt_id;
        CirStmt_toGoto(stmt_id, 0);
        if (CirValue_getU64(value))
            CirCode_addTrueJump(code_id, stmt_id);
        else
            CirCode_addFalseJump(code_id, stmt_id);
    } else if (CirValue_isString(value)) {
        // True jump only
        CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
        if (firstStmt)
            *firstStmt = stmt_id;
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addTrueJump(code_id, stmt_id);
    } else {
        // True jump and false jump
        CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
        CirStmt_toCmp(stmt_id, CIR_CONDOP_NE, value, CirValue_ofI64(CIR_IINT, 0), 0);
        if (firstStmt)
            *firstStmt = stmt_id;
        CirCode_addTrueJump(code_id, stmt_id);
        stmt_id = CirCode_appendNewStmt(code_id);
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addFalseJump(code_id, stmt_id);
    }
}

CirCodeId
CirBuild__land(CirCodeId lhs, CirCodeId rhs)
{
    if (CirCode_isExpr(lhs)) {
        const CirValue *value = CirCode_getValue(lhs);
        if (!value)
            cir_fatal("&&: lhs has no value");

        // Is this a constant?
        bool lhsIsAlwaysTrue = false, lhsIsAlwaysFalse = false;
        if (CirValue_isInt(value)) {
            if (CirValue_getU64(value))
                lhsIsAlwaysTrue = true;
            else
                lhsIsAlwaysFalse = true;
        } else if (CirValue_isString(value)) {
            // Strings are always true
            lhsIsAlwaysTrue = true;
        }

        if (lhsIsAlwaysTrue) {
            // Depends on whether rhs is an expr or cond as well
            if (CirCode_isExpr(rhs)) {
                const CirValue *rhsValue = CirCode_getValue(rhs);
                if (!value)
                    cir_fatal("&&: rhs has no value");

                // Is rhs a constant?
                if (CirValue_isInt(rhsValue)) {
                    CirCode_append(lhs, rhs);
                    CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, !!CirValue_getU64(rhsValue)));
                } else if (CirValue_isString(rhsValue)) {
                    CirCode_append(lhs, rhs);
                    CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, 1));
                } else {
                    toCond(rhs, NULL);
                    CirCode_append(lhs, rhs);
                }
                return lhs;
            } else {
                CirCode_append(lhs, rhs);
                return lhs;
            }
        } else if (lhsIsAlwaysFalse) {
            // Run lhs only.
            CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, 0));
            return lhs;
        }

        toCond(lhs, NULL);
        // Fallthrough to the rest of the code to process the conditional lhs
    }

    // Lhs is a cond, will need to do backpatching
    assert(CirCode_isCond(lhs));
    // Rhs must be a cond
    CirStmtId rhsFirstStmt = CirCode_getFirstStmt(rhs);
    if (CirCode_isExpr(rhs))
        toCond(rhs, rhsFirstStmt ? NULL : &rhsFirstStmt);
    assert(rhsFirstStmt); // If rhs was already a cond, then it will have at least one stmt.
    backpatch(&codes[lhs].truejumps, rhsFirstStmt);
    CirCode_append(lhs, rhs);
    return lhs;
}

CirCodeId
CirBuild__lor(CirCodeId lhs, CirCodeId rhs)
{
    if (CirCode_isExpr(lhs)) {
        const CirValue *value = CirCode_getValue(lhs);
        if (!value)
            cir_fatal("||: lhs has no value");

        // Is this a constant?
        bool lhsIsAlwaysTrue = false, lhsIsAlwaysFalse = false;
        if (CirValue_isInt(value)) {
            if (CirValue_getU64(value))
                lhsIsAlwaysTrue = true;
            else
                lhsIsAlwaysFalse = true;
        } else if (CirValue_isString(value)) {
            // Strings are always true
            lhsIsAlwaysTrue = true;
        }

        if (lhsIsAlwaysTrue) {
            // Run lhs only.
            CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, 1));
            return lhs;
        } else if (lhsIsAlwaysFalse) {
            // Depends on whether rhs is an expr or cond as well
            if (CirCode_isExpr(rhs)) {
                const CirValue *rhsValue = CirCode_getValue(rhs);
                if (!value)
                    cir_fatal("||: rhs has no value");

                // Is rhs a constant?
                if (CirValue_isInt(rhsValue)) {
                    CirCode_append(lhs, rhs);
                    CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, !!CirValue_getU64(rhsValue)));
                } else if (CirValue_isString(rhsValue)) {
                    CirCode_append(lhs, rhs);
                    CirCode_setValue(lhs, CirValue_ofI64(CIR_IINT, 1));
                } else {
                    toCond(rhs, NULL);
                    CirCode_append(lhs, rhs);
                }
                return lhs;
            } else {
                CirCode_append(lhs, rhs);
                return lhs;
            }
        }

        toCond(lhs, NULL);
        // Fallthrough to the rest of the code to process the conditional lhs
    }

    // Lhs is a cond, will need to do backpatching
    assert(CirCode_isCond(lhs));
    // Rhs must be a cond
    CirStmtId rhsFirstStmt = CirCode_getFirstStmt(rhs);
    if (CirCode_isExpr(rhs))
        toCond(rhs, rhsFirstStmt ? NULL : &rhsFirstStmt);
    assert(rhsFirstStmt); // If rhs was already a cond, then it will have at least one stmt.
    backpatch(&codes[lhs].falsejumps, rhsFirstStmt);
    CirCode_append(lhs, rhs);
    return lhs;
}

CirCodeId
CirBuild__if(CirCodeId condCode, CirCodeId thenCode, CirCodeId elseCode)
{
    assert(condCode);

    // The type of code we generate depends on whether condCode is a constant
    if (CirCode_isExpr(condCode)) {
        const CirValue *value = CirCode_getValue(condCode);
        if (!value)
            cir_fatal("if: conditional expression has no value");

        // Is this a constant?
        if (CirValue_isInt(value)) {
            // Only need to generate thenCode/elseCode
            uint64_t val = CirValue_getU64(value);
            if (val) {
                // thenCode only
                if (thenCode)
                    CirCode_append(condCode, thenCode);
                CirCode_setValue(condCode, NULL);
                return condCode;
            } else {
                // elseCode only
                if (elseCode)
                    CirCode_append(condCode, elseCode);
                CirCode_setValue(condCode, NULL);
                return condCode;
            }
        }

        toCond(condCode, NULL);
        // Fallthrough to the rest of the code to process the condCode
    }

    // Is a Cond, will need to do backpatching
    assert(CirCode_isCond(condCode));
    bool willGenThen = thenCode && CirCode_getFirstStmt(thenCode);
    bool willGenElse = elseCode && CirCode_getFirstStmt(elseCode);
    CirStmtId thenGotoStmt = 0, elseGotoStmt = 0;
    if (thenCode) {
        assert(CirCode_isExpr(thenCode));
        CirStmtId thenFirstStmt = CirCode_getFirstStmt(thenCode);
        CirCode_append(condCode, thenCode);
        if (thenFirstStmt) {
            backpatch(&codes[condCode].truejumps, thenFirstStmt);
            // If we are not generating the else block, can just fall-through
            if (willGenElse) {
                thenGotoStmt = CirCode_appendNewStmt(condCode);
                CirStmt_toGoto(thenGotoStmt, 0);
            }
        }
    }

    if (elseCode) {
        assert(CirCode_isExpr(elseCode));
        CirStmtId elseFirstStmt = CirCode_getFirstStmt(elseCode);
        CirCode_append(condCode, elseCode);
        if (elseFirstStmt) {
            backpatch(&codes[condCode].falsejumps, elseFirstStmt);
            // If we are not generating the then block, can just fall-through
            if (willGenThen) {
                elseGotoStmt = CirCode_appendNewStmt(condCode);
                CirStmt_toGoto(elseGotoStmt, 0);
            }
        }
    }

    if (codes[condCode].truejumps.len || codes[condCode].falsejumps.len || thenGotoStmt || elseGotoStmt) {
        CirStmtId restStmt = CirCode_appendNewStmt(condCode);
        backpatch(&codes[condCode].truejumps, restStmt);
        backpatch(&codes[condCode].falsejumps, restStmt);
        if (thenGotoStmt)
            CirStmt_setJumpTarget(thenGotoStmt, restStmt);
        if (elseGotoStmt)
            CirStmt_setJumpTarget(elseGotoStmt, restStmt);
    }

    // All truejumps/falsejumps should have been consumed
    assert(codes[condCode].truejumps.len == 0);
    assert(codes[condCode].falsejumps.len == 0);
    condCode = CirCode_toExpr(condCode, true);
    return condCode;
}

CirCodeId
CirBuild__for(CirCodeId condCode, CirStmtId firstStmt, CirCodeId thenCode, CirCodeId afterCode)
{
    assert(condCode);

    if (afterCode)
        afterCode = CirCode_toExpr(afterCode, true);

    // The type of code we generate depends on whether condCode is a constant
    if (CirCode_isExpr(condCode)) {
        const CirValue *value = CirCode_getValue(condCode);
        if (!value)
            cir_fatal("for: conditional expression has no value");

        // Is this a constant?
        bool genInfiniteLoop = false, genNoLoop = false;
        if (CirValue_isInt(value)) {
            uint64_t val = CirValue_getU64(value);
            if (val)
                genInfiniteLoop = true;
            else
                genNoLoop = true;
        } else if (CirValue_isString(value)) {
            // Strings are always true
            genInfiniteLoop = true;
        }

        if (genInfiniteLoop) {
            if (!firstStmt && thenCode)
                firstStmt = CirCode_getFirstStmt(thenCode);
            if (!firstStmt && afterCode)
                firstStmt = CirCode_getFirstStmt(afterCode);
            CirStmtId afterStmt = afterCode ? CirCode_getFirstStmt(afterCode) : 0;
            if (!afterStmt)
                afterStmt = firstStmt;
            CirStmtId thenStartStmt = 0, thenEndStmt = 0;
            if (thenCode) {
                thenStartStmt = CirCode_getFirstStmt(thenCode);
                thenEndStmt = CirCode_getLastStmt(thenCode);
            }
            CirCode_append(condCode, thenCode);
            CirCode_append(condCode, afterCode);
            CirStmtId gotoStmt = CirCode_appendNewStmt(condCode);
            CirStmt_toGoto(gotoStmt, firstStmt ? firstStmt : gotoStmt);
            CirCode_setValue(condCode, NULL);
            // Resolve breaks and continues within thenCode
            CirStmtId restStmt = 0;
            for (CirStmtId stmt_id = thenStartStmt; stmt_id; stmt_id = CirStmt_getNext(stmt_id)) {
                if (CirStmt_isBreak(stmt_id)) {
                    if (!restStmt)
                        restStmt = CirCode_appendNewStmt(condCode);
                    CirStmt_toGoto(stmt_id, restStmt);
                } else if (CirStmt_isContinue(stmt_id)) {
                    assert(afterStmt); // If there is at least one stmt in thenCode, then afterStmt will not be 0
                    CirStmt_toGoto(stmt_id, afterStmt);
                }
                if (stmt_id == thenEndStmt)
                    break;
            }
            return condCode;
        } else if (genNoLoop) {
            CirCode_setValue(condCode, NULL);
            return condCode;
        }

        toCond(condCode, firstStmt ? NULL : &firstStmt);
        // Fallthrough to the rest of the code to process the condCode
    }

    // Is a Cond, will need to do backpatching
    assert(CirCode_isCond(condCode));
    assert(firstStmt);
    CirStmtId afterStmt = afterCode ? CirCode_getFirstStmt(afterCode) : 0;
    if (!afterStmt)
        afterStmt = firstStmt;
    CirStmtId thenStartStmt = 0, thenEndStmt = 0;
    if (thenCode) {
        thenStartStmt = CirCode_getFirstStmt(thenCode);
        thenEndStmt = CirCode_getLastStmt(thenCode);
    }

    CirCodeId trueFirstStmt;
    if (thenCode && (trueFirstStmt = CirCode_getFirstStmt(thenCode))) {
        assert(CirCode_isExpr(thenCode));
        CirCode_append(condCode, thenCode);
        CirCode_append(condCode, afterCode);
        backpatch(&codes[condCode].truejumps, trueFirstStmt);
        CirCodeId gotoStmt = CirCode_appendNewStmt(condCode);
        CirStmt_toGoto(gotoStmt, firstStmt);
    } else if (afterCode && (trueFirstStmt = CirCode_getFirstStmt(afterCode))) {
        assert(CirCode_isExpr(afterCode));
        CirCode_append(condCode, afterCode);
        backpatch(&codes[condCode].truejumps, trueFirstStmt);
        CirCodeId gotoStmt = CirCode_appendNewStmt(condCode);
        CirStmt_toGoto(gotoStmt, firstStmt);
    } else {
        backpatch(&codes[condCode].truejumps, firstStmt);
    }

    CirStmtId restStmt = CirCode_appendNewStmt(condCode);
    backpatch(&codes[condCode].falsejumps, restStmt);

    // Resolve breaks and continues within thenCode
    for (CirStmtId stmt_id = thenStartStmt; stmt_id; stmt_id = CirStmt_getNext(stmt_id)) {
        if (CirStmt_isBreak(stmt_id))
            CirStmt_toGoto(stmt_id, restStmt);
        else if (CirStmt_isContinue(stmt_id))
            CirStmt_toGoto(stmt_id, afterStmt);
        if (stmt_id == thenEndStmt)
            break;
    }

    // All truejumps/falsejumps should have been consumed
    assert(codes[condCode].truejumps.len == 0);
    assert(codes[condCode].falsejumps.len == 0);
    condCode = CirCode_toExpr(condCode, true);
    return condCode;

}

CirCodeId
CirBuild__lnot(CirCodeId condCode)
{
    assert(condCode);

    // The type of code we generate depends on whether condCode is a constant
    if (CirCode_isExpr(condCode)) {
        const CirValue *value = CirCode_getValue(condCode);
        if (!value)
            cir_fatal("lnot: conditional expression has no value");

        // Is this a constant?
        if (CirValue_isInt(value)) {
            uint64_t val = CirValue_getU64(value);
            CirCode_setValue(condCode, CirValue_ofI64(CIR_IINT, !val));
            return condCode;
        }

        toCond(condCode, NULL);
        // Fallthrough to the rest of the code to process the condCode
    }

    // Swap truejumps/falsejumps
    {
        CirStmtIdArray tmp = codes[condCode].truejumps;
        codes[condCode].truejumps = codes[condCode].falsejumps;
        codes[condCode].falsejumps = tmp;
    }
    return condCode;
}

CirCodeId
CirBuild__addrof(CirCodeId code_id)
{
    if (CirCode_isCond(code_id))
        cir_fatal("addrof: operand is a temporary");
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("addrof: operand has no value");
    if (!CirValue_isLval(value))
        cir_fatal("addrof: operand is not a lvalue");
    if (CirValue_getNumFields(value) || CirValue_isVar(value)) {
        // Must use addrof with a temporary
        CirVarId tmp_id = CirVar_new(code_id);
        const CirType *type = CirValue_getType(value);
        if (type) {
            const CirType *unrolledType = CirType_unroll(type);
            if (CirType_isArray(unrolledType) || CirType_isFun(unrolledType))
                CirVar_setType(tmp_id, CirType_lvalConv(type));
            else
                CirVar_setType(tmp_id, CirType_ptr(type));
        }
        const CirValue *tmp_value = CirValue_ofVar(tmp_id);
        CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
        CirStmt_toUnOp(stmt_id, tmp_value, CIR_UNOP_ADDROF, value);
        CirCode_setValue(code_id, tmp_value);
    } else {
        // Mem with no field access, equivalent to &(*x).
        // Convert back to Var.
        assert(CirValue_isMem(value));
        CirCode_setValue(code_id, CirValue_ofVar(CirValue_getVar(value)));
    }
    return code_id;
}

CirCodeId
CirBuild__deref(CirCodeId code_id)
{
    if (CirCode_isCond(code_id))
        cir_fatal("deref: operand does not have pointer type");
    const CirValue *value = CirCode_getValue(code_id);
    if (!value)
        cir_fatal("deref: operand has no value");
    const CirType *valueType = CirValue_getType(value);
    if (valueType)
        valueType = CirType_lvalConv(valueType);
    if (CirValue_getNumFields(value) || CirValue_isMem(value)) {
        // Must use temporary
        CirVarId tmp_id = CirVar_new(code_id);
        if (valueType)
            CirVar_setType(tmp_id, valueType);
        const CirValue *tmp_value = CirValue_ofVar(tmp_id);
        CirStmtId stmt_id = CirCode_appendNewStmt(code_id);
        CirStmt_toUnOp(stmt_id, tmp_value, CIR_UNOP_IDENTITY, value);
        CirCode_setValue(code_id, CirValue_ofMem(tmp_id));
    } else {
        // Var with no field access, equivalent to *x.
        // Convert to Mem.
        assert(CirValue_isVar(value));
        CirCode_setValue(code_id, CirValue_ofMem(CirValue_getVar(value)));
    }
    return code_id;
}
