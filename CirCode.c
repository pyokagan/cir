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

#define BINOP_TEMPLATE(name) \
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
    if (CirValue_isLval(lhs_value) || CirValue_isLval(rhs_value)) \
        goto runtime; \
    else \
        goto comptime

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
        CirCode_setValue(lhs, CirValue_ofVar(dst_id)); \
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
    lhs_type = CirType_unroll(lhs_type);
    rhs_type = CirType_unroll(rhs_type);
    if (CirType_isArithmetic(lhs_type) && CirType_isArithmetic(rhs_type)) {
        return CirBuild__plusA(lhs, rhs, mach);
    } else if (CirType_isPtr(lhs_type) && CirType_isInt(rhs_type)) {
        cir_bug("TODO: CirBuild__plusPi");
        //return CirBuild__plusPi(lhs, rhs);
    } else if (CirType_isInt(lhs_type) && CirType_isPtr(rhs_type)) {
        // Temporarily swap the values of the two codes,
        // so that the stmts will be appended in the correct order (lhs first)
        CirCode_setValue(lhs, rhs_value);
        CirCode_setValue(rhs, lhs_value);
        cir_bug("TODO: CirBuild__plusPi");
        // return CirBuild__plusPi(lhs, rhs);
    } else {
        cir_fatal("Invalid operands to binary plus operator");
    }
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
        cir_bug("TODO: MinusPI");
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

        // Need to generate a runtime cmp
        CirCode__toEmptyCond(condCode);
        CirStmtId stmt_id = CirCode_appendNewStmt(condCode);
        CirStmt_toCmp(stmt_id, CIR_CONDOP_NE, value, CirValue_ofI64(CIR_IINT, 0), 0);
        CirCode_addTrueJump(condCode, stmt_id);
        stmt_id = CirCode_appendNewStmt(condCode);
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addFalseJump(condCode, stmt_id);
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
CirBuild__while(CirCodeId condCode, CirStmtId firstStmt, CirCodeId thenCode)
{
    assert(condCode);

    // The type of code we generate depends on whether condCode is a constant
    if (CirCode_isExpr(condCode)) {
        const CirValue *value = CirCode_getValue(condCode);
        if (!value)
            cir_fatal("while: conditional expression has no value");

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
            firstStmt = firstStmt ? firstStmt : CirCode_getFirstStmt(thenCode);
            CirCode_append(condCode, thenCode);
            CirStmtId gotoStmt = CirCode_appendNewStmt(condCode);
            CirStmt_toGoto(gotoStmt, firstStmt ? firstStmt : gotoStmt);
            CirCode_setValue(condCode, NULL);
            return condCode;
        } else if (genNoLoop) {
            CirCode_setValue(condCode, NULL);
            return condCode;
        }

        // Need to generate a runtime cmp
        CirCode__toEmptyCond(condCode);
        CirStmtId stmt_id = CirCode_appendNewStmt(condCode);
        CirStmt_toCmp(stmt_id, CIR_CONDOP_NE, value, CirValue_ofI64(CIR_IINT, 0), 0);
        if (!firstStmt)
            firstStmt = stmt_id;
        CirCode_addTrueJump(condCode, stmt_id);
        stmt_id = CirCode_appendNewStmt(condCode);
        CirStmt_toGoto(stmt_id, 0);
        CirCode_addFalseJump(condCode, stmt_id);
        // Fallthrough to the rest of the code to process the condCode
    }

    // Is a Cond, will need to do backpatching
    assert(CirCode_isCond(condCode));
    assert(firstStmt);
    CirCodeId thenFirstStmt;
    if (thenCode && (thenFirstStmt = CirCode_getFirstStmt(thenCode))) {
        assert(CirCode_isExpr(thenCode));
        CirCode_append(condCode, thenCode);
        backpatch(&codes[condCode].truejumps, thenFirstStmt);
        CirCodeId gotoStmt = CirCode_appendNewStmt(condCode);
        CirStmt_toGoto(gotoStmt, firstStmt);
    } else {
        backpatch(&codes[condCode].truejumps, firstStmt);
    }

    CirStmtId restStmt = CirCode_appendNewStmt(condCode);
    backpatch(&codes[condCode].falsejumps, restStmt);

    // All truejumps/falsejumps should have been consumed
    assert(codes[condCode].truejumps.len == 0);
    assert(codes[condCode].falsejumps.len == 0);
    condCode = CirCode_toExpr(condCode, true);
    return condCode;
}