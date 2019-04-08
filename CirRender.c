#include "cir_internal.h"
#include <stdlib.h>
#include <assert.h>

typedef struct RenderItem {
    enum {
        RENDER_COMP_DEF,
        RENDER_COMP_DECL,
        RENDER_TYPEDEF,
        RENDER_VAR_DECL,
        RENDER_FUN_DEF
    } type;
    CirCompId comp_id;
    CirTypedefId typedef_id;
    CirVarId var_id;
} RenderItem;
typedef CirArray(RenderItem) RenderItemArray;

__attribute__((unused)) static void
RenderItem_log(const RenderItem *item)
{
    switch (item->type) {
    case RENDER_COMP_DEF:
        CirLog_print("RENDER_COMP_DEF(");
        CirComp_log(item->comp_id);
        CirLog_print(")");
        break;
    case RENDER_COMP_DECL:
        CirLog_printf("RENDER_COMP_DECL(");
        CirComp_log(item->comp_id);
        CirLog_print(")");
        break;
    case RENDER_TYPEDEF:
        CirLog_print("RENDER_TYPEDEF(");
        CirTypedef_log(item->typedef_id);
        CirLog_print(")");
        break;
    case RENDER_VAR_DECL:
        CirLog_print("RENDER_VAR_DECL(");
        CirVar_logNameAndType(item->var_id);
        CirLog_print(")");
        break;
    case RENDER_FUN_DEF:
        CirLog_print("RENDER_FUN_DEF(");
        CirVar_logNameAndType(item->var_id);
        CirLog_print(")");
        break;
    }
}

#define STATUS_NOT_VISITED 0
#define STATUS_VISITING 1
#define STATUS_VISITING_DECLARED 2
#define STATUS_VISITED 3

static void orderType(RenderItemArray *, uint8_t *cidStatus, uint8_t *tidStatus, const CirType *, bool);
static void orderTypedef(RenderItemArray *, uint8_t *cidStatus, uint8_t *tidStatus, CirTypedefId, bool);
static void orderComp(RenderItemArray *, uint8_t *cidStatus, uint8_t *tidStatus, CirCompId, bool);
static void orderVar(RenderItemArray *, uint8_t *vidStatus, uint8_t *cidStatus, uint8_t *tidStatus, uint8_t *stmtStatus, CirVarId);

static void
orderType(RenderItemArray *out, uint8_t *cidStatus, uint8_t *tidStatus, const CirType *type, bool mustDef)
{
    if (CirType_isVoid(type) || CirType_isInt(type) || CirType_isFloat(type) || CirType_isVaList(type)) {
        // Built-in types, no need any further rendering.
    } else if (CirType_isPtr(type)) {
        const CirType *bt = CirType_getBaseType(type);
        orderType(out, cidStatus, tidStatus, bt, false);
    } else if (CirType_isArray(type)) {
        const CirType *bt = CirType_getBaseType(type);
        orderType(out, cidStatus, tidStatus, bt, mustDef);
    } else if (CirType_isFun(type)) {
        const CirType *bt = CirType_getBaseType(type);
        orderType(out, cidStatus, tidStatus, bt, false);
        size_t numParams = CirType_getNumParams(type);
        const CirFunParam *params = CirType_getParams(type);
        for (size_t i = 0; i < numParams; i++) {
            orderType(out, cidStatus, tidStatus, params[i].type, false);
        }
    } else if (CirType_isNamed(type)) {
        CirTypedefId tid = CirType_getTypedefId(type);
        orderTypedef(out, cidStatus, tidStatus, tid, mustDef);
    } else if (CirType_isComp(type)) {
        CirCompId cid = CirType_getCompId(type);
        orderComp(out, cidStatus, tidStatus, cid, mustDef);
    } else {
        cir_bug("orderType: unhandled type");
    }
}

static void
orderTypedef(RenderItemArray *out, uint8_t *cidStatus, uint8_t *tidStatus, CirTypedefId tid, bool mustDef)
{
    if (tidStatus[tid] == STATUS_VISITED)
        return; // Already defined
    else if (tidStatus[tid] == STATUS_VISITING || tidStatus[tid] == STATUS_VISITING_DECLARED)
        cir_fatal("circular dependency");

    tidStatus[tid] = STATUS_VISITING;
    const CirType *type = CirTypedef_getType(tid);
    orderType(out, cidStatus, tidStatus, type, mustDef);
    RenderItem item;
    item.type = RENDER_TYPEDEF;
    item.typedef_id = tid;
    CirArray_push(out, &item);
    tidStatus[tid] = STATUS_VISITED;
}

static void
orderComp(RenderItemArray *out, uint8_t *cidStatus, uint8_t *tidStatus, CirCompId cid, bool mustDef)
{
    if (cidStatus[cid] == STATUS_VISITED) {
        return; // Already defined
    } else if (cidStatus[cid] == STATUS_VISITING || cidStatus[cid] == STATUS_VISITING_DECLARED) {
        if (mustDef)
            cir_fatal("circular dependency");
        if (cidStatus[cid] == STATUS_VISITING_DECLARED)
            return;
        // Forward declaration
        RenderItem item;
        item.type = RENDER_COMP_DECL;
        item.comp_id = cid;
        CirArray_push(out, &item);
        cidStatus[cid] = STATUS_VISITING_DECLARED;
        return;
    }

    if (!CirComp_isDefined(cid)) {
        if (mustDef) {
            CirLog_begin(CIRLOG_FATAL);
            CirLog_print("missing a definition: ");
            CirComp_log(cid);
            CirLog_end();
            exit(1);
        }
        // Forward declaration
        RenderItem item;
        item.type = RENDER_COMP_DECL;
        item.comp_id = cid;
        CirArray_push(out, &item);
        cidStatus[cid] = STATUS_VISITED;
        return;
    }

    // Try to define whenever possible
    cidStatus[cid] = STATUS_VISITING;
    size_t numFields = CirComp_getNumFields(cid);
    for (size_t i = 0; i < numFields; i++) {
        const CirType *type = CirComp_getFieldType(cid, i);
        orderType(out, cidStatus, tidStatus, type, mustDef);
    }
    RenderItem item;
    item.type = RENDER_COMP_DEF;
    item.comp_id = cid;
    CirArray_push(out, &item);
    cidStatus[cid] = STATUS_VISITED;
}

static void
orderValue(RenderItemArray *out, uint8_t *vidStatus, uint8_t *cidStatus, uint8_t *tidStatus, uint8_t *stmtStatus, const CirValue *value, CirVarId parentVid)
{
    if (CirValue_isString(value) || CirValue_isInt(value) || CirValue_isUser(value)) {
        // Do nothing
    } else if (CirValue_isType(value)) {
        orderType(out, cidStatus, tidStatus, CirValue_getTypeValue(value), false);
    } else if (CirValue_isLval(value)) {
        CirVarId vid = CirValue_getVar(value);
        if (vid != parentVid)
            orderVar(out, vidStatus, cidStatus, tidStatus, stmtStatus, vid);
    } else {
        cir_bug("orderValue: unhandled value case");
    }
}

static void
orderVar(RenderItemArray *out, uint8_t *vidStatus, uint8_t *cidStatus, uint8_t *tidStatus, uint8_t *stmtStatus, CirVarId vid)
{
    if (vidStatus[vid] == STATUS_VISITED) {
        return; // Already declared/defined
    } else if (vidStatus[vid] == STATUS_VISITING || vidStatus[vid] == STATUS_VISITING_DECLARED) {
        if (vidStatus[vid] == STATUS_VISITING_DECLARED)
            return;
        RenderItem item;
        item.type = RENDER_VAR_DECL;
        item.var_id = vid;
        CirArray_push(out, &item);
        vidStatus[vid] = STATUS_VISITING_DECLARED;
    }

    CirCodeId owner = CirVar_getOwner(vid);
    if (owner) {
        // We do not need to "define" the variable in global, however we do need to define its types.
        const CirType *type = CirVar_getType(vid);
        if (type) // May still be __auto_type
            orderType(out, cidStatus, tidStatus, type, true);
        vidStatus[vid] = STATUS_VISITED;
        return;
    }

    vidStatus[vid] = STATUS_VISITING;
    const CirType *type = CirVar_getType(vid);
    if (type) // May still be __auto_type
        orderType(out, cidStatus, tidStatus, type, false);
    CirCodeId code_id = CirVar_getCode(vid);
    if (code_id) {
        CirStmtId stmt_id = CirCode_getFirstStmt(code_id);
        while (stmt_id) {
            if (CirStmt_isUnOp(stmt_id)) {
                const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand1, vid);
                const CirValue *dst = CirStmt_getDst(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, dst, vid);
            } else if (CirStmt_isBinOp(stmt_id)) {
                const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand1, vid);
                const CirValue *operand2 = CirStmt_getOperand2(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand2, vid);
                const CirValue *dst = CirStmt_getDst(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, dst, vid);
            } else if (CirStmt_isCall(stmt_id)) {
                const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand1, vid);
                size_t numArgs = CirStmt_getNumArgs(stmt_id);
                for (size_t i = 0; i < numArgs; i++) {
                    const CirValue *value = CirStmt_getArg(stmt_id, i);
                    if (value)
                        orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, value, vid);
                }
                const CirValue *dst = CirStmt_getDst(stmt_id);
                if (dst)
                    orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, dst, vid);
            } else if (CirStmt_isReturn(stmt_id)) {
                const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
                if (operand1)
                    orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand1, vid);
            } else if (CirStmt_isCmp(stmt_id)) {
                CirStmtId jumpTarget = CirStmt_getJumpTarget(stmt_id);
                stmtStatus[jumpTarget] = 1;
                const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand1, vid);
                const CirValue *operand2 = CirStmt_getOperand2(stmt_id);
                orderValue(out, vidStatus, cidStatus, tidStatus, stmtStatus, operand2, vid);
            } else if (CirStmt_isGoto(stmt_id)) {
                CirStmtId jumpTarget = CirStmt_getJumpTarget(stmt_id);
                stmtStatus[jumpTarget] = 1;
            }
            stmt_id = CirStmt_getNext(stmt_id);
        }

        RenderItem item;
        item.type = RENDER_FUN_DEF;
        item.var_id = vid;
        CirArray_push(out, &item);
    } else {
        // TODO: Sometimes we do have variable definitions... (initializers)
        RenderItem item;
        item.type = RENDER_VAR_DECL;
        item.var_id = vid;
        CirArray_push(out, &item);
    }
    vidStatus[vid] = STATUS_VISITED;
}

static bool
isRenderRoot(CirVarId var_id)
{
    if (CirVar_getOwner(var_id))
        return false;
    const CirType *type = CirVar_getType(var_id);
    assert(type);
    type = CirType_unroll(type);
    CirStorage storage = CirVar_getStorage(var_id);
    if (CirType_isFun(type)) {
        return storage != CIR_STATIC && CirVar_getCode(var_id);
    } else {
        return storage != CIR_STATIC && storage != CIR_EXTERN;
    }
}

static void
stdoutPrinter(const void *ptr, size_t len)
{
    fwrite(ptr, len, 1, stdout);
}

static void
renderComp(CirCompId comp_id, bool def)
{
    printf(CirComp_isStruct(comp_id) ? "struct cid%u" : "union cid%u", (unsigned)comp_id);
    CirName name = CirComp_getName(comp_id);
    if (name)
        printf("_%s", CirName_cstr(name));

    if (!def) {
        printf(";\n");
        return;
    }

    printf(" {\n");
    size_t numFields = CirComp_getNumFields(comp_id);
    for (size_t i = 0; i < numFields; i++) {
        CirName name = CirComp_getFieldName(comp_id, i);
        const CirType *type = CirComp_getFieldType(comp_id, i);
        assert(type);
        printf("    ");
        CirType_print(stdoutPrinter, type, CirName_cstr(name), 0, true);
        printf(";\n");
    }
    printf("};\n");
}

static void
renderTypedef(CirTypedefId tid)
{
    char buf[1024];

    printf("typedef ");
    const CirType *type = CirTypedef_getType(tid);
    CirName name = CirTypedef_getName(tid);
    if (name)
        snprintf(buf, sizeof(buf), "tid%u_%s", (unsigned)tid, CirName_cstr(name));
    else
        snprintf(buf, sizeof(buf), "tid%u", (unsigned)tid);
    CirType_print(stdoutPrinter, type, buf, 0, true);
    printf(";\n");
}

static void
renderVar(CirVarId var_id, bool def, uint8_t *vidStatus, uint8_t *stmtStatus)
{
    CirVar_printDecl(stdoutPrinter, var_id, true);
    if (!def) {
        printf(";\n");
        return;
    }

    const CirType *type = CirVar_getType(var_id);
    const CirType *unrolledType = CirType_unroll(type);
    CirCodeId code_id = CirVar_getCode(var_id);
    assert(code_id);
    assert(CirType_isFun(unrolledType));
    printf("\n{\n");

    // Declarations
    const CirVarId *formals = CirVar__getFormals(var_id);
    size_t numFormals = CirType_getNumParams(unrolledType);
    size_t numVars = CirCode_getNumVars(code_id);
    bool printedDecl = false;
    for (size_t i = 0; i < numVars; i++) {
        CirVarId local_id = CirCode_getVar(code_id, i);
        assert(CirVar_getOwner(local_id) == code_id);
        if (vidStatus[local_id] != STATUS_VISITED)
            continue;
        for (size_t j = 0; j < numFormals; j++) {
            if (formals[j] == local_id)
                goto is_formal;
        }
        printedDecl = true;
        printf("    ");
        CirVar_printDecl(stdoutPrinter, local_id, true);
        printf(";\n");
is_formal:
        continue;
    }

    // Statements
    if (printedDecl)
        printf("\n");
    CirStmtId stmt_id = CirCode_getFirstStmt(code_id);
    while (stmt_id) {
        if (stmtStatus[stmt_id])
            printf("sid%" PRIu32 ":\n", stmt_id);
        printf("    ");
        CirStmt_print(stdoutPrinter, stmt_id, true);
        printf("\n");
        stmt_id = CirStmt_getNext(stmt_id);
    }
    printf("}\n");
}

void
CirRender(void)
{
    size_t numComps = CirComp_getNum();
    size_t numTypedefs = CirTypedef_getNum();
    size_t numVars = CirVar_getNum();
    size_t numStmts = CirStmt_getNum();

    uint8_t *cidStatus = calloc(numComps, sizeof(uint8_t));
    if (!cidStatus)
        cir_fatal("out of memory");
    uint8_t *tidStatus = calloc(numTypedefs, sizeof(uint8_t));
    if (!tidStatus)
        cir_fatal("out of memory");
    uint8_t *vidStatus = calloc(numVars, sizeof(uint8_t));
    if (!vidStatus)
        cir_fatal("out of memory");
    uint8_t *stmtStatus = calloc(numStmts, sizeof(uint8_t));
    if (!stmtStatus)
        cir_fatal("out of memory");

    RenderItemArray arr = CIRARRAY_INIT;

    // First get ordering
    for (size_t i = 1; i < numVars; i++) {
        CirVarId vid = i;
        if (isRenderRoot(vid))
            orderVar(&arr, vidStatus, cidStatus, tidStatus, stmtStatus, vid);
    }

#if 0
    // Debug: print ordering
    CirLog_begin(CIRLOG_DEBUG);
    for (size_t i = 0; i < arr.len; i++) {
        RenderItem_log(&arr.items[i]);
        CirLog_print("\n");
    }
    CirLog_end();
#endif


    // Do printing
    for (size_t i = 0; i < arr.len; i++) {
        switch (arr.items[i].type) {
        case RENDER_COMP_DEF:
            renderComp(arr.items[i].comp_id, true);
            break;
        case RENDER_COMP_DECL:
            renderComp(arr.items[i].comp_id, false);
            break;
        case RENDER_TYPEDEF:
            renderTypedef(arr.items[i].typedef_id);
            break;
        case RENDER_VAR_DECL:
            renderVar(arr.items[i].var_id, false, vidStatus, stmtStatus);
            break;
        case RENDER_FUN_DEF:
            renderVar(arr.items[i].var_id, true, vidStatus, stmtStatus);
            break;
        default:
            cir_bug("unhandled render case");
        }
    }

    CirArray_release(&arr);
    free(vidStatus);
    free(cidStatus);
    free(tidStatus);
}
