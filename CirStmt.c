#include "cir_internal.h"
#include <assert.h>

#define MAX_STMT (1024 * 1024 * 1)

#define CIR_STMT_NOP 0
#define CIR_STMT_UNOP 1
#define CIR_STMT_BINOP 2
#define CIR_STMT_CALL 3
#define CIR_STMT_RETURN 4
#define CIR_STMT_CMP 5
#define CIR_STMT_GOTO 6
#define CIR_STMT_LABEL 7
#define CIR_STMT_GOTO_LABEL 8
#define CIR_STMT_USER 9

#define MAX_UID 63

// data1:
// bits 31 to 28: type
// bits 27 to 22: op / uid (6 bits)
// bit 21: hasPrev
// bit 20: hasNext

#define data1ToType(x) (((x) >> 28) & 0x0f)
#define typeToData1(x) (((x) & 0x0f) << 28)
#define data1ClearType(x) ((x) & ~(0x0f << 28))
#define data1ToOp(x) (((x) >> 22) & 0x3f)
#define opToData1(x) (((x) & 0x3f) << 22)
#define data1ClearOp(x) ((x) & ~(0x3f << 22))
#define data1ToHasPrev(x) (((x) >> 21) & 0x01)
#define hasPrevToData1(x) (((x) & 0x01) << 21)
#define data1ToHasNext(x) (((x) >> 20) & 0x01)
#define hasNextToData1(x) (((x) & 0x01) << 20)

typedef CirArray(const CirValue *) CirValueArray;

typedef struct CirStmt {
    uint32_t data1;
    union {
        CirStmtId stmt;
        CirCodeId code;
    } prev;
    union {
        CirStmtId stmt;
        CirCodeId code;
    } next;
    union {
        const CirValue *dst;
        CirStmtId jumpTarget;
        CirName labelName;
        void *ptr;
    } u1;
    const CirValue *operand1;
    const CirValue *operand2;
    CirValueArray args;
} CirStmt;

static CirStmt stmts[MAX_STMT];
static uint32_t numStmts = 1;

static unsigned uidCtr = 1;

unsigned
CirStmt_registerUser(void)
{
    if (uidCtr > MAX_UID)
        cir_bug("too many registered user stmt types");
    return uidCtr++;
}

static void
CirStmt__setType(CirStmtId sid, uint32_t type)
{
    stmts[sid].data1 = data1ClearType(stmts[sid].data1) | typeToData1(type);
}

static void
CirStmt__setOp(CirStmtId sid, uint32_t op)
{
    stmts[sid].data1 = data1ClearOp(stmts[sid].data1) | opToData1(op);
}

static void
CirStmt__setUid(CirStmtId sid, uint32_t uid)
{
    CirStmt__setOp(sid, uid);
}

static bool
CirStmt__hasNext(CirStmtId sid)
{
    return data1ToHasNext(stmts[sid].data1);
}

void
CirStmt__setNextStmt(CirStmtId sid, CirStmtId next_sid)
{
    stmts[sid].next.stmt = next_sid;
    stmts[sid].data1 |= hasNextToData1(1);
}

static CirCodeId
CirStmt__getNextCode(CirStmtId sid)
{
    assert(!CirStmt__hasNext(sid));
    return stmts[sid].next.code;
}

void
CirStmt__setNextCode(CirStmtId sid, CirCodeId code)
{
    stmts[sid].next.code = code;
    stmts[sid].data1 &= ~hasNextToData1(1);
}

static bool
CirStmt__hasPrev(CirStmtId sid)
{
    return data1ToHasPrev(stmts[sid].data1);
}

void
CirStmt__setPrevStmt(CirStmtId sid, CirStmtId prev_sid)
{
    stmts[sid].prev.stmt = prev_sid;
    stmts[sid].data1 |= hasPrevToData1(1);
}

static CirCodeId
CirStmt__getPrevCode(CirStmtId sid)
{
    assert(!CirStmt__hasPrev(sid));
    return stmts[sid].prev.code;
}

void
CirStmt__setPrevCode(CirStmtId sid, CirCodeId code)
{
    stmts[sid].prev.code = code;
    stmts[sid].data1 &= ~hasPrevToData1(1);
}

static CirStmtId
CirStmt__alloc(void)
{
    if (numStmts >= MAX_STMT)
        cir_bug("too many CirStmts");
    CirStmtId sid = numStmts++;
    stmts[sid].data1 = 0;
    return sid;
}

CirStmtId
CirStmt__new(CirCodeId code)
{
    CirStmtId sid = CirStmt__alloc();
    stmts[sid].prev.code = code;
    stmts[sid].next.code = code;
    return sid;
}

CirStmtId
CirStmt_newOrphan(void)
{
    CirStmtId stmtId = CirStmt__new(0);
    assert(CirStmt_isOrphan(stmtId));
    return stmtId;
}

CirStmtId
CirStmt_newAfter(CirStmtId prev)
{
    assert(prev != 0);

    if (CirStmt__hasNext(prev)) {
        // Insertion in the middle
        CirStmtId sid = CirStmt__alloc();
        CirStmtId next = CirStmt_getNext(prev);
        CirStmt__setPrevStmt(sid, prev);
        CirStmt__setNextStmt(sid, next);
        CirStmt__setNextStmt(prev, sid);
        CirStmt__setPrevStmt(next, sid);
        return sid;
    } else {
        // Insertion at the end
        CirCodeId code = stmts[prev].next.code;
        assert(code);
        CirStmtId sid = CirStmt__new(code);
        CirStmt__setPrevStmt(sid, prev);
        CirStmt__setNextStmt(prev, sid);
        CirCode__setLastStmt(code, sid);
        return sid;
    }
}

CirStmtId
CirStmt_newBefore(CirStmtId next)
{
    assert(next);

    if (CirStmt__hasPrev(next)) {
        // Insertion in the middle
        CirStmtId sid = CirStmt__alloc();
        CirStmtId prev = CirStmt_getPrev(next);
        CirStmt__setPrevStmt(sid, prev);
        CirStmt__setNextStmt(sid, next);
        CirStmt__setNextStmt(prev, sid);
        CirStmt__setPrevStmt(next, sid);
        return sid;
    } else {
        // Insertion at the beginning
        CirCodeId code = CirStmt__getPrevCode(next);
        assert(code);
        CirStmtId sid = CirStmt__new(code);
        CirStmt__setNextStmt(sid, next);
        CirStmt__setPrevStmt(next, sid);
        CirCode__setFirstStmt(code, sid);
        return sid;
    }
}

void
CirStmt_orphanize(CirStmtId stmtId)
{
    assert(stmtId);
    if (CirStmt__hasNext(stmtId)) {
        CirStmtId next = CirStmt_getNext(stmtId);
        if (CirStmt__hasPrev(stmtId))
            CirStmt__setPrevStmt(next, CirStmt_getPrev(stmtId));
        else
            CirStmt__setPrevCode(next, CirStmt__getPrevCode(stmtId));
    } else {
        CirCodeId codeId = CirStmt__getNextCode(stmtId);
        if (CirStmt__hasPrev(stmtId))
            CirCode__setLastStmt(codeId, CirStmt_getPrev(stmtId));
        else
            CirCode__setLastStmt(codeId, 0);
    }
    if (CirStmt__hasPrev(stmtId)) {
        CirStmtId prev = CirStmt_getPrev(stmtId);
        if (CirStmt__hasNext(stmtId))
            CirStmt__setNextStmt(prev, CirStmt_getNext(stmtId));
        else
            CirStmt__setNextCode(prev, CirStmt__getNextCode(stmtId));
    } else {
        CirCodeId codeId = CirStmt__getPrevCode(stmtId);
        if (CirStmt__hasNext(stmtId))
            CirCode__setFirstStmt(codeId, CirStmt_getNext(stmtId));
        else
            CirCode__setFirstStmt(codeId, 0);
    }
    CirStmt__setPrevCode(stmtId, 0);
    CirStmt__setNextCode(stmtId, 0);
    assert(CirStmt_isOrphan(stmtId));
}

bool
CirStmt_isOrphan(CirStmtId stmtId)
{
    assert(stmtId);
    return !CirStmt__hasNext(stmtId) && !CirStmt__hasPrev(stmtId) && !stmts[stmtId].next.code;
}

void
CirStmt_toNop(CirStmtId sid)
{
    assert(sid != 0);
    CirStmt__setType(sid, CIR_STMT_NOP);
    stmts[sid].u1.dst = NULL;
    stmts[sid].operand1 = NULL;
    stmts[sid].operand2 = NULL;
}

void
CirStmt_toUnOp(CirStmtId sid, const CirValue *dst, uint32_t unop, const CirValue *operand1)
{
    assert(sid != 0);
    assert(dst != NULL);
    assert(operand1 != NULL);
    CirStmt__setType(sid, CIR_STMT_UNOP);
    CirStmt__setOp(sid, unop);
    stmts[sid].u1.dst = dst;
    stmts[sid].operand1 = operand1;
    stmts[sid].operand2 = NULL;
}

void
CirStmt_toBinOp(CirStmtId sid, const CirValue *dst, uint32_t binop, const CirValue *operand1, const CirValue *operand2)
{
    assert(sid != 0);
    assert(dst != NULL);
    assert(operand1 != NULL);
    assert(operand2 != NULL);
    CirStmt__setType(sid, CIR_STMT_BINOP);
    CirStmt__setOp(sid, binop);
    stmts[sid].u1.dst = dst;
    stmts[sid].operand1 = operand1;
    stmts[sid].operand2 = operand2;
}

void
CirStmt_toCall(CirStmtId sid, const CirValue *dst, const CirValue *target, const CirValue *const *args, size_t numArgs)
{
    assert(sid != 0);
    assert(target != NULL);

    CirStmt__setType(sid, CIR_STMT_CALL);
    stmts[sid].u1.dst = dst;
    stmts[sid].operand1 = target;
    stmts[sid].operand2 = NULL;
    if (args != stmts[sid].args.items) {
        CirArray_alloc(&stmts[sid].args, numArgs);
        memcpy(stmts[sid].args.items, args, sizeof(const CirValue *) * numArgs);
    }
    stmts[sid].args.len = numArgs;
}

void
CirStmt_toReturn(CirStmtId sid, const CirValue *value)
{
    assert(sid != 0);

    CirStmt__setType(sid, CIR_STMT_RETURN);
    stmts[sid].u1.dst = NULL;
    stmts[sid].operand1 = value;
    stmts[sid].operand2 = NULL;
}

void
CirStmt_toCmp(CirStmtId stmt_id, uint32_t condop, const CirValue *op1, const CirValue *op2, CirStmtId jumpTarget)
{
    assert(stmt_id != 0);
    assert(op1);
    assert(op2);

    CirStmt__setType(stmt_id, CIR_STMT_CMP);
    CirStmt__setOp(stmt_id, condop);
    stmts[stmt_id].operand1 = op1;
    stmts[stmt_id].operand2 = op2;
    stmts[stmt_id].u1.jumpTarget = jumpTarget;
}

void
CirStmt_toGoto(CirStmtId stmt_id, CirStmtId jumpTarget)
{
    assert(stmt_id != 0);

    CirStmt__setType(stmt_id, CIR_STMT_GOTO);
    stmts[stmt_id].operand1 = NULL;
    stmts[stmt_id].operand2 = NULL;
    stmts[stmt_id].u1.jumpTarget = jumpTarget;
}

void
CirStmt_toLabel(CirStmtId stmtId, CirName labelName)
{
    assert(stmtId != 0);
    CirStmt__setType(stmtId, CIR_STMT_LABEL);
    stmts[stmtId].u1.labelName = labelName;
}

void
CirStmt_toGotoLabel(CirStmtId stmtId, CirName labelName)
{
    assert(stmtId != 0);
    CirStmt__setType(stmtId, CIR_STMT_GOTO_LABEL);
    stmts[stmtId].u1.labelName = labelName;
}

void
CirStmt_toUser(CirStmtId stmtId, unsigned uid, void *ptr)
{
    assert(stmtId);
    assert(uid <= MAX_UID);

    CirStmt__setType(stmtId, CIR_STMT_USER);
    CirStmt__setUid(stmtId, uid);
    stmts[stmtId].u1.ptr = ptr;
}

bool
CirStmt_isNop(CirStmtId sid)
{
    assert(sid != 0);
    return data1ToType(stmts[sid].data1) == CIR_STMT_NOP;
}

bool
CirStmt_isUnOp(CirStmtId sid)
{
    assert(sid != 0);
    return data1ToType(stmts[sid].data1) == CIR_STMT_UNOP;
}

bool
CirStmt_isBinOp(CirStmtId sid)
{
    assert(sid != 0);
    return data1ToType(stmts[sid].data1) == CIR_STMT_BINOP;
}

bool
CirStmt_isCall(CirStmtId sid)
{
    assert(sid != 0);
    return data1ToType(stmts[sid].data1) == CIR_STMT_CALL;
}

bool
CirStmt_isReturn(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    return data1ToType(stmts[stmt_id].data1) == CIR_STMT_RETURN;
}

bool
CirStmt_isCmp(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    return data1ToType(stmts[stmt_id].data1) == CIR_STMT_CMP;
}

bool
CirStmt_isGoto(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    return data1ToType(stmts[stmt_id].data1) == CIR_STMT_GOTO;
}

bool
CirStmt_isJump(CirStmtId stmt_id)
{
    return CirStmt_isCmp(stmt_id) || CirStmt_isGoto(stmt_id);
}

bool
CirStmt_isLabel(CirStmtId stmtId)
{
    assert(stmtId);
    return data1ToType(stmts[stmtId].data1) == CIR_STMT_LABEL;
}

bool
CirStmt_isGotoLabel(CirStmtId stmtId)
{
    assert(stmtId);
    return data1ToType(stmts[stmtId].data1) == CIR_STMT_GOTO_LABEL;
}

unsigned
CirStmt_isUser(CirStmtId stmtId)
{
    assert(stmtId);
    if (data1ToType(stmts[stmtId].data1) != CIR_STMT_USER)
        return 0;
    return data1ToOp(stmts[stmtId].data1);
}

uint32_t
CirStmt_getOp(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isUnOp(stmt_id) || CirStmt_isBinOp(stmt_id) || CirStmt_isCmp(stmt_id));
    return data1ToOp(stmts[stmt_id].data1);
}

const CirValue *
CirStmt_getDst(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isUnOp(stmt_id) || CirStmt_isBinOp(stmt_id) || CirStmt_isCall(stmt_id));
    return stmts[stmt_id].u1.dst;
}

const CirValue *
CirStmt_getOperand1(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isUnOp(stmt_id) || CirStmt_isBinOp(stmt_id) || CirStmt_isCall(stmt_id) || CirStmt_isReturn(stmt_id) || CirStmt_isCmp(stmt_id));
    return stmts[stmt_id].operand1;
}

const CirValue *
CirStmt_getOperand2(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isBinOp(stmt_id) || CirStmt_isCmp(stmt_id));
    return stmts[stmt_id].operand2;
}

size_t
CirStmt_getNumOperands(CirStmtId stmtId)
{
    switch (data1ToType(stmts[stmtId].data1)) {
    case CIR_STMT_NOP:
        return 0;
    case CIR_STMT_UNOP:
        return 2; // dst + operand1
    case CIR_STMT_BINOP:
        return 3; // dst + operand1 + operand2
    case CIR_STMT_CALL:
        return 2 + CirStmt_getNumArgs(stmtId); // dst + target + args
    case CIR_STMT_RETURN:
        return 1;
    case CIR_STMT_CMP:
        return 2; // operand1 + operand2
    case CIR_STMT_GOTO:
        return 0;
    case CIR_STMT_LABEL:
        return 0;
    case CIR_STMT_GOTO_LABEL:
        return 0;
    case CIR_STMT_USER:
        return 0;
    default:
        cir_bug("unhandled case");
    }
}

const CirValue *
CirStmt_getOperand(CirStmtId stmtId, size_t i)
{
    if (i >= CirStmt_getNumOperands(stmtId))
        cir_fatal("CirStmt_getOperand: invalid operand index");

    switch (data1ToType(stmts[stmtId].data1)) {
    case CIR_STMT_UNOP:
        return i == 0 ? stmts[stmtId].u1.dst : stmts[stmtId].operand1;
    case CIR_STMT_BINOP:
        return i == 0 ? stmts[stmtId].u1.dst :
                i == 1 ? stmts[stmtId].operand1 :
                stmts[stmtId].operand2;
    case CIR_STMT_CALL:
        return i == 0 ? stmts[stmtId].u1.dst :
                i == 1 ? stmts[stmtId].operand1 :
                stmts[stmtId].args.items[i - 2];
    case CIR_STMT_RETURN:
        return stmts[stmtId].operand1;
    case CIR_STMT_CMP:
        return i ? stmts[stmtId].operand2 : stmts[stmtId].operand1;
    case CIR_STMT_NOP:
    case CIR_STMT_GOTO:
    case CIR_STMT_USER:
    default:
        cir_bug("unhandled case");
    }
}

void
CirStmt_setOperand(CirStmtId stmtId, size_t i, const CirValue *value)
{
    if (i >= CirStmt_getNumOperands(stmtId))
        cir_fatal("CirStmt_setOperand: invalid operand index");

    switch (data1ToType(stmts[stmtId].data1)) {
    case CIR_STMT_UNOP:
        if (!value)
            cir_fatal("value cannot be NULL");
        if (i == 0)
            stmts[stmtId].u1.dst = value;
        else
            stmts[stmtId].operand1 = value;
        return;
    case CIR_STMT_BINOP:
        if (!value)
            cir_fatal("value cannot be NULL");
        if (i == 0)
            stmts[stmtId].u1.dst = value;
        else if (i == 1)
            stmts[stmtId].operand1 = value;
        else
            stmts[stmtId].operand2 = value;
        return;
    case CIR_STMT_CALL:
        if (i == 0) {
            stmts[stmtId].u1.dst = value;
            return;
        }
        if (!value)
            cir_fatal("value cannot be NULL");
        if (i == 1)
            stmts[stmtId].operand1 = value;
        else
            stmts[stmtId].args.items[i - 2] = value;
        return;
    case CIR_STMT_RETURN:
        stmts[stmtId].operand1 = value;
        return;
    case CIR_STMT_CMP:
        if (!value)
            cir_fatal("value cannot be NULL");
        if (i)
            stmts[stmtId].operand2 = value;
        else
            stmts[stmtId].operand1 = value;
        return;
    case CIR_STMT_NOP:
    case CIR_STMT_GOTO:
    case CIR_STMT_USER:
    case CIR_STMT_LABEL:
    case CIR_STMT_GOTO_LABEL:
    default:
        cir_bug("unhandled case");
    }
}

size_t
CirStmt_getNumArgs(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isCall(stmt_id));
    return stmts[stmt_id].args.len;
}

const CirValue *
CirStmt_getArg(CirStmtId stmt_id, size_t i)
{
    assert(stmt_id != 0);
    assert(CirStmt_isCall(stmt_id));
    assert(i < stmts[stmt_id].args.len);
    return stmts[stmt_id].args.items[i];
}

CirStmtId
CirStmt_getJumpTarget(CirStmtId stmt_id)
{
    assert(stmt_id != 0);
    assert(CirStmt_isJump(stmt_id));
    return stmts[stmt_id].u1.jumpTarget;
}

void
CirStmt_setJumpTarget(CirStmtId stmt_id, CirStmtId jumpTarget)
{
    assert(stmt_id != 0);
    assert(CirStmt_isJump(stmt_id));
    stmts[stmt_id].u1.jumpTarget = jumpTarget;
}

CirName
CirStmt_getLabelName(CirStmtId stmtId)
{
    assert(stmtId != 0);
    assert(CirStmt_isLabel(stmtId) || CirStmt_isGotoLabel(stmtId));
    return stmts[stmtId].u1.labelName;
}

void
CirStmt_setLabelName(CirStmtId stmtId, CirName labelName)
{
    assert(stmtId != 0);
    assert(CirStmt_isLabel(stmtId) || CirStmt_isGotoLabel(stmtId));
    stmts[stmtId].u1.labelName = labelName;
}

void *
CirStmt_getPtr(CirStmtId stmtId)
{
    assert(stmtId);
    assert(CirStmt_isUser(stmtId));
    return stmts[stmtId].u1.ptr;
}

void
CirStmt_setPtr(CirStmtId stmtId, void *ptr)
{
    assert(stmtId);
    assert(CirStmt_isUser(stmtId));
    stmts[stmtId].u1.ptr = ptr;
}

CirStmtId
CirStmt_getNext(CirStmtId sid)
{
    assert(sid != 0);
    return CirStmt__hasNext(sid) ? stmts[sid].next.stmt : 0;
}

CirStmtId
CirStmt_getPrev(CirStmtId sid)
{
    assert(sid != 0);
    return CirStmt__hasPrev(sid) ? stmts[sid].prev.stmt : 0;
}

static const char *unopToStr[] = {
    [CIR_UNOP_NEG] = "-",
    [CIR_UNOP_BNOT] = "~",
    [CIR_UNOP_LNOT] = "!",
    [CIR_UNOP_ADDROF] = "&",
    [CIR_UNOP_IDENTITY] = "",
};

static const char *binopToStr[] = {
    [CIR_BINOP_PLUS] = " + ",
    [CIR_BINOP_MINUS] = " - ",
    [CIR_BINOP_MUL] = " * ",
    [CIR_BINOP_DIV] = " / ",
    [CIR_BINOP_MOD] = " % ",
    [CIR_BINOP_SHIFTLT] = " << ",
    [CIR_BINOP_SHIFTRT] = " >> ",
    [CIR_BINOP_BAND] = " & ",
    [CIR_BINOP_BXOR] = " ^ ",
    [CIR_BINOP_BOR] = " | ",
};

static const char *condopToStr[] = {
    [CIR_CONDOP_LT] = " < ",
    [CIR_CONDOP_GT] = " > ",
    [CIR_CONDOP_LE] = " <= ",
    [CIR_CONDOP_GE] = " >= ",
    [CIR_CONDOP_EQ] = " == ",
    [CIR_CONDOP_NE] = " != ",
};

void
CirStmt_print(CirFmt printer, CirStmtId sid, bool renderName)
{
    switch (data1ToType(stmts[sid].data1)) {
    case CIR_STMT_NOP:
        CirFmt_printString(printer, "/* nop */");
        break;
    case CIR_STMT_UNOP:
        CirValue_print(printer, stmts[sid].u1.dst, renderName);
        CirFmt_printString(printer, " = ");
        CirFmt_printString(printer, unopToStr[data1ToOp(stmts[sid].data1)]);
        CirValue_print(printer, stmts[sid].operand1, renderName);
        break;
    case CIR_STMT_BINOP:
        CirValue_print(printer, stmts[sid].u1.dst, renderName);
        CirFmt_printString(printer, " = ");
        CirValue_print(printer, stmts[sid].operand1, renderName);
        CirFmt_printString(printer, binopToStr[data1ToOp(stmts[sid].data1)]);
        CirValue_print(printer, stmts[sid].operand2, renderName);
        break;
    case CIR_STMT_CALL:
        if (stmts[sid].u1.dst) {
            CirValue_print(printer, stmts[sid].u1.dst, renderName);
            CirFmt_printString(printer, " = ");
        }
        CirValue_print(printer, stmts[sid].operand1, renderName);
        CirFmt_printString(printer, "(");
        for (size_t i = 0; i < stmts[sid].args.len; i++) {
            if (i)
                CirFmt_printString(printer, ", ");
            CirValue_print(printer, stmts[sid].args.items[i], renderName);
        }
        CirFmt_printString(printer, ")");
        break;
    case CIR_STMT_RETURN:
        CirFmt_printString(printer, "return");
        if (stmts[sid].operand1) {
            CirFmt_printString(printer, " ");
            CirValue_print(printer, stmts[sid].operand1, renderName);
        }
        break;
    case CIR_STMT_CMP:
        CirFmt_printString(printer, "if (");
        CirValue_print(printer, stmts[sid].operand1, renderName);
        CirFmt_printString(printer, condopToStr[data1ToOp(stmts[sid].data1)]);
        CirValue_print(printer, stmts[sid].operand2, renderName);
        CirFmt_printString(printer, ") goto ");
        if (stmts[sid].u1.jumpTarget) {
            CirFmt_printString(printer, "sid");
            CirFmt_printU32(printer, stmts[sid].u1.jumpTarget);
        } else {
            CirFmt_printString(printer, "<CirStmt 0>");
        }
        break;
    case CIR_STMT_GOTO:
        CirFmt_printString(printer, "goto ");
        if (stmts[sid].u1.jumpTarget) {
            CirFmt_printString(printer, "sid");
            CirFmt_printU32(printer, stmts[sid].u1.jumpTarget);
        } else {
            CirFmt_printString(printer, "<CirStmt 0>");
        }
        break;
    case CIR_STMT_LABEL:
        CirFmt_printString(printer, CirName_cstr(stmts[sid].u1.labelName));
        CirFmt_printString(printer, ":");
        break;
    case CIR_STMT_GOTO_LABEL:
        CirFmt_printString(printer, "goto ");
        CirFmt_printString(printer, CirName_cstr(stmts[sid].u1.labelName));
        break;
    case CIR_STMT_USER:
        CirFmt_printString(printer, "USER ");
        CirFmt_printU32(printer, data1ToOp(stmts[sid].data1));
        break;
    default:
        cir_bug("CirStmt_log: unexpected stmt type");
    }

    CirFmt_printString(printer, "; /* sid");
    CirFmt_printU32(printer, sid);
    CirFmt_printString(printer, " */");
}

void
CirStmt_log(CirStmtId sid)
{
    if (sid == 0) {
        CirLog_print("<CirStmt 0>");
        return;
    }

    CirStmt_print(CirLog_printb, sid, false);
}

size_t
CirStmt_getNum(void)
{
    return numStmts;
}

void
CirStmt_typecheck(CirStmtId stmtId, const CirMachine *mach)
{
    if (CirStmt_isNop(stmtId)) {
        // Do nothing
    } else if (CirStmt_isUnOp(stmtId)) {
        uint32_t op = CirStmt_getOp(stmtId);
        const CirValue *dst = CirStmt_getDst(stmtId);
        if (!dst)
            cir_fatal("unop stmt has no dst");
        if (!CirValue_isLval(dst))
            cir_fatal("unop dst is not an lval");
        CirVarId dstVarId = CirValue_getVar(dst);
        const CirValue *operand = CirStmt_getOperand1(stmtId);
        if (!operand)
            cir_fatal("unop stmt has no operand");
        const CirType *operandType = CirValue_getType(operand);
        if (!operandType)
            cir_fatal("unop operand has no type");
        const CirType *outType = CirType_ofUnOp(op, operandType, mach);
        if (!CirVar_getType(dstVarId))
            CirVar_setType(dstVarId, outType);
    } else if (CirStmt_isBinOp(stmtId)) {
        uint32_t op = CirStmt_getOp(stmtId);
        const CirValue *dst = CirStmt_getDst(stmtId);
        if (!dst)
            cir_fatal("binop stmt has no dst");
        if (!CirValue_isLval(dst))
            cir_fatal("binop dst is not an lval");
        CirVarId dstVarId = CirValue_getVar(dst);
        const CirValue *operand1 = CirStmt_getOperand1(stmtId);
        if (!operand1)
            cir_fatal("binop stmt has no operand1");
        const CirType *operand1Type = CirValue_getType(operand1);
        if (!operand1Type)
            cir_fatal("binop operand1 has no type");
        const CirValue *operand2 = CirStmt_getOperand2(stmtId);
        if (!operand2)
            cir_fatal("binop stmt has no operand2");
        const CirType *operand2Type = CirValue_getType(operand2);
        if (!operand2Type)
            cir_fatal("binop operand2 has no type");
        const CirType *outType = CirType_ofBinOp(op, operand1Type, operand2Type, mach);
        if (!CirVar_getType(dstVarId))
            CirVar_setType(dstVarId, outType);
    } else if (CirStmt_isCall(stmtId)) {
        const CirValue *dst = CirStmt_getDst(stmtId);
        if (dst && !CirValue_isLval(dst))
            cir_fatal("call dst is not an lval");
        CirVarId dstVarId;
        if (dst)
            dstVarId = CirValue_getVar(dst);
        const CirValue *target = CirStmt_getOperand1(stmtId);
        if (!target)
            cir_fatal("call stmt has no target");
        const CirType *targetType = CirValue_getType(target);
        if (!targetType)
            cir_fatal("call target has no type");
        const CirType *outType = CirType_ofCall(targetType);
        if (dst && !CirVar_getType(dstVarId))
            CirVar_setType(dstVarId, outType);
    } else if (CirStmt_isReturn(stmtId)) {
        // TODO: typecheck operand
    } else if (CirStmt_isCmp(stmtId)) {
        // TODO: typecheck operands
    } else if (CirStmt_isGoto(stmtId)) {
        // Do nothing
    } else if (CirStmt_isUser(stmtId)) {
        // TODO: Do nothing???
    } else {
        cir_bug("unhandled stmt type");
    }
}
