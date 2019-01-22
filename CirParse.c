#include "cir_internal.h"
#include <assert.h>
#include <stdlib.h>

static const CirMachine *CirParse__mach;

#define DECLARATOR_CONCRETE 0
#define DECLARATOR_ABSTRACT 1
#define DECLARATOR_MAYBEABSTRACT 2

typedef struct CirParse__TypeSpecItem {
    enum {
        CIRPARSE_TVOID = 1,
        CIRPARSE_TCHAR,
        CIRPARSE_TBOOL,
        CIRPARSE_TSHORT,
        CIRPARSE_TINT,
        CIRPARSE_TLONG,
        CIRPARSE_TFLOAT,
        CIRPARSE_TDOUBLE,
        CIRPARSE_TSIGNED,
        CIRPARSE_TUNSIGNED,
        CIRPARSE_TNAMED,
        CIRPARSE_TAUTOTYPE,
        CIRPARSE_TCOMP,
        CIRPARSE_TBUILTIN_VA_LIST
    } type;
    union {
        CirName name;
        CirTypedefId tid;
        CirCompId cid;
    } data;
} CirParse__TypeSpecItem;
typedef CirArray(CirParse__TypeSpecItem) CirParse__TypeSpecArray;

typedef struct CirParse__ProcessedSpec {
    const CirType *baseType; // NULL = autotype
    uint8_t storage; // storage kind
    bool isInline; // inline
    bool isTypedef; // typedef
    CirAttrArray attrArray;
} CirParse__ProcessedSpec;

static void
CirParse__ProcessedSpec_init(CirParse__ProcessedSpec *pspec)
{
    CirArray_init(&pspec->attrArray);
}

static void
CirParse__ProcessedSpec_release(CirParse__ProcessedSpec *pspec)
{
    CirArray_release(&pspec->attrArray);
}

// Declarator item
typedef CirArray(CirFunParam) CirFunParamArray;
typedef struct CirParse__DeclItem {
    enum {
        CIRPARSE_DTARRAY = 1,
        CIRPARSE_DTPTR,
        CIRPARSE_DTPROTO,
        CIRPARSE_DTPAREN
    } type;
    CirAttrArray attrs;
    CirAttrArray rattrs;
    CirFunParamArray funParams;
    bool isva;
    bool hasLen; // Has array length
    uint32_t arrayLen;
} CirParse__DeclItem;
typedef CirArray(CirParse__DeclItem) CirParse__DeclArray;

static bool decl_spec_list_FIRST(void);
static void declaration_or_function_definition(CirCodeId);
static void comp_field_declaration(CirCompId);
static const CirType *type_name(int);
static CirCodeId comma_expression(void);
static CirCodeId expression(void);
static CirCodeId block(bool dropValue);

static void
CirParse__DeclArray_release(CirParse__DeclArray *arr)
{
    for (size_t i = 0; i < arr->len; i++) {
        CirArray_release(&arr->items[i].attrs);
        CirArray_release(&arr->items[i].rattrs);
        CirArray_release(&arr->items[i].funParams);
    }
    CirArray_release(arr);
}

__attribute__((noreturn))
static void
unexpected_token(const char *ctx, const char *expectedWhat)
{
    CirLog_begin(CIRLOG_FATAL);
    CirLog_print(ctx);
    CirLog_print(": unexpected token ");
    CirLog_print(CirLex__str(cirtok.type));
    CirLog_print(", expected ");
    CirLog_print(expectedWhat);
    CirLog_end();
    exit(1);
}

static CirVarId
makeGlobalVar(CirVarId vid)
{
    CirVarId old_vid;
    CirTypedefId tid;
    CirName name = CirVar_getName(vid);
    assert(name);
    int result = CirEnv__findGlobalName(name, &old_vid, &tid);
    if (!result) {
        return vid;
    } else if (result == 2) {
        cir_fatal("declared as a different type of symbol: %s", CirName_cstr(name));
    } else {
        assert(result == 1);
        assert(old_vid);
        const CirType *oldType = CirVar_getType(old_vid);
        assert(oldType);
        const CirType *newType = CirVar_getType(vid);
        if (!newType)
            cir_fatal("cannot use __auto_type in re-declaration of global: %s", CirName_cstr(name));
        // It was already defined. We must re-use the varinfo. But clean up the storage
        // TODO: set storage

        const CirType *combinedType = CirType__combine(oldType, newType);
        if (!combinedType)
            cir_fatal("Declaration of %s does not match previous declaration", CirName_cstr(name));

        CirVar_setType(old_vid, combinedType);
        return old_vid;
    }
}

// Returns NUL-terminated string literal
static char *
string_literal(size_t *outSize)
{
    assert(cirtok.type == CIRTOK_STRINGLIT);
    CirBBuf buf = CIRBBUF_INIT;
    while (cirtok.type == CIRTOK_STRINGLIT) {
        CirBBuf_grow(&buf, cirtok.data.stringlit.len);
        memcpy(buf.items + buf.len, cirtok.data.stringlit.buf, cirtok.data.stringlit.len);
        buf.len += cirtok.data.stringlit.len;
        CirLex__next();
    }
    // Finally NUL-terminate
    CirBBuf_grow(&buf, 1);
    buf.items[buf.len++] = 0;
    char *out = cir__balloc(buf.len);
    memcpy(out, buf.items, buf.len);
    if (outSize)
        *outSize = buf.len;
    CirBBuf_release(&buf);
    return out;
}

static const CirAttr *
attr(void)
{
    switch (cirtok.type) {
    case CIRTOK_IDENT:
    case CIRTOK_TYPENAME: {
        CirName name = cirtok.data.name;
        CirLex__next();
        if (cirtok.type != CIRTOK_LPAREN)
            return CirAttr_name(name);

        // Is a Cons
        CirLex__next(); // Consume Lparen
        if (cirtok.type == CIRTOK_RPAREN) {
            // Empty Cons
            CirLex__next();
            return CirAttr_cons(name, NULL, 0);
        }

        CirAttrArray args = CIRARRAY_INIT;
        for (;;) {
            const CirAttr *arg = attr();
            CirArray_push(&args, &arg);
            if (cirtok.type == CIRTOK_RPAREN) {
                CirLex__next();
                break;
            } else if (cirtok.type == CIRTOK_COMMA) {
                // OK, expecting next attr
                CirLex__next();
            } else {
                unexpected_token("attr", "`,` or `)`");
            }
        }
        const CirAttr *ret = CirAttr_cons(name, args.items, args.len);
        CirArray_release(&args);
        return ret;
    }
    case CIRTOK_LPAREN: {
        CirLex__next();
        const CirAttr *nestedAttr = attr();
        if (cirtok.type != CIRTOK_RPAREN)
            unexpected_token("attr", "`)`");
        CirLex__next();
        return nestedAttr;
    }
    case CIRTOK_INTLIT: {
        int32_t value = cirtok.data.intlit.val.i64;
        CirLex__next();
        return CirAttr_int(value);
    }
    case CIRTOK_STRINGLIT: {
        char *buf = string_literal(NULL);
        return CirAttr_str(buf);
    }
    default:
        unexpected_token("attr", "IDENT, TYPENAME, `(`, INTLIT, STRINGLIT");
    }
}

static bool
attribute_list_FIRST(bool with_asm, bool with_cv)
{
    return cirtok.type == CIRTOK_ATTRIBUTE ||
            (with_cv && cirtok.type == CIRTOK_CONST) ||
            (with_cv && cirtok.type == CIRTOK_RESTRICT) ||
            (with_cv && cirtok.type == CIRTOK_VOLATILE) ||
            (with_asm && cirtok.type == CIRTOK_ASM);
}

static void
attribute_list(CirAttrArray *out, bool with_asm, bool with_cv)
{
    assert(attribute_list_FIRST(with_asm, with_cv));

loop:
    if (cirtok.type == CIRTOK_ATTRIBUTE) {
        CirLex__next();
        if (cirtok.type != CIRTOK_LPAREN)
            cir_fatal("expected `(`");
        CirLex__next();
        if (cirtok.type != CIRTOK_LPAREN)
            cir_fatal("expected `(`");
        CirLex__next();
        if (cirtok.type == CIRTOK_RPAREN)
            goto attribute_finish;
        for (;;) {
            if (cirtok.type != CIRTOK_IDENT && cirtok.type != CIRTOK_TYPENAME)
                cir_fatal("expected ident or typename, got %s", CirLex__str(cirtok.type));
            const CirAttr *a = attr();
            assert(CirAttr_isName(a) || CirAttr_isCons(a));
            CirAttrArray__add(out, a);
            if (cirtok.type == CIRTOK_RPAREN) {
                break;
            } else if (cirtok.type == CIRTOK_COMMA) {
                // Expecting another attribute
                CirLex__next();
            } else {
                unexpected_token("__attribute__", "`,`, `)`");
            }
        }
attribute_finish:
        CirLex__next();
        if (cirtok.type != CIRTOK_RPAREN)
            unexpected_token("__attribute__", "`)`");
        CirLex__next();
        goto loop;
    } else if (with_cv && cirtok.type == CIRTOK_CONST) {
        CirLex__next();
        const CirAttr *attr = CirAttr_name(CirName_of("const"));
        CirAttrArray__add(out, attr);
        goto loop;
    } else if (with_cv && cirtok.type == CIRTOK_RESTRICT) {
        CirLex__next();
        const CirAttr *attr = CirAttr_name(CirName_of("restrict"));
        CirAttrArray__add(out, attr);
        goto loop;
    } else if (with_cv && cirtok.type == CIRTOK_VOLATILE) {
        CirLex__next();
        const CirAttr *attr = CirAttr_name(CirName_of("volatile"));
        CirAttrArray__add(out, attr);
        goto loop;
    } else if (with_asm && cirtok.type == CIRTOK_ASM) {
        // In some contexts we can have an inline assembly to specify the name
        // to be used for a global.
        // We treat this as a name attribute.
        CirLex__next();

        if (cirtok.type != CIRTOK_LPAREN)
            unexpected_token("__asm__", "`(`");
        CirLex__next();

        if (cirtok.type != CIRTOK_STRINGLIT)
            unexpected_token("__asm__", "STRINGLIT");
        char *buf = string_literal(NULL);

        if (cirtok.type != CIRTOK_RPAREN)
            unexpected_token("__asm__", "`)`");
        CirLex__next();

        CirName name = CirName_of("__asm__");
        const CirAttr *args[1] = { CirAttr_str(buf) };
        const CirAttr *attr = CirAttr_cons(name, args, 1);
        CirAttrArray__add(out, attr);
        goto loop;
    } else {
        return;
    }
}

static CirCodeId
primary_expression(void)
{
    switch (cirtok.type) {
    case CIRTOK_INTLIT: {
        const CirValue *val;
        if (CirIkind_isSigned(cirtok.data.intlit.ikind, CirParse__mach)) {
            val = CirValue_ofI64(cirtok.data.intlit.ikind, cirtok.data.intlit.val.i64);
        } else {
            val = CirValue_ofU64(cirtok.data.intlit.ikind, cirtok.data.intlit.val.u64);
        }
        CirLex__next(); // consume intlit
        return CirCode_ofExpr(val);
    }
    case CIRTOK_STRINGLIT: {
        size_t len;
        char *buf = string_literal(&len);
        const CirValue *val = CirValue_ofString(buf, len);
        return CirCode_ofExpr(val);
    }
    case CIRTOK_IDENT: {
        CirVarId vid;
        CirTypedefId tid;

        if (CirEnv__findLocalName(cirtok.data.name, &vid, &tid) == 1) {
            const CirValue *val = CirValue_ofVar(vid);
            CirLex__next(); // consume ident
            return CirCode_ofExpr(val);
        } else {
            cir_fatal("unknown ident: %s", CirName_cstr(cirtok.data.name));
        }
    }
    case CIRTOK_AT: {
        CirCodeId code_id;

        // compile-time evaluation
        CirLex__next();

        if (cirtok.type != CIRTOK_IDENT)
            unexpected_token("comp_eval", "IDENT");

        CirVarId vid;
        CirTypedefId tid;
        if (CirEnv__findLocalName(cirtok.data.name, &vid, &tid) != 1)
            cir_fatal("comp_eval: unknown ident: %s", CirName_cstr(cirtok.data.name));

        const CirType *type = CirVar_getType(vid);
        if (!CirType_isFun(type)) {
            CirLog_begin(CIRLOG_FATAL);
            CirLog_print("comp_eval: not a function type: ");
            CirType_log(type, CirName_cstr(cirtok.data.name));
        }

        CirLex__next();

        if (cirtok.type != CIRTOK_LPAREN)
            unexpected_token("comp_eval", "`(`");
        CirLex__next();

        // Collect function arguments.
        CirArray(CirCodeId) args = CIRARRAY_INIT;

        // Are we calling with any arguments?
        if (cirtok.type == CIRTOK_RPAREN) {
            CirLex__next();
            goto build_function_call;
        }

        for (;;) {
            CirCodeId argCode = expression();
            assert(CirCode_isExpr(argCode));
            CirArray_push(&args, &argCode);
            if (cirtok.type == CIRTOK_COMMA) {
                CirLex__next();
            } else if (cirtok.type == CIRTOK_RPAREN) {
                CirLex__next();
                break;
            } else {
                unexpected_token("comp_eval", "`,`, `)`");
            }
        }

build_function_call:
        code_id = CirX64_call(vid, args.items, args.len);
        CirArray_release(&args);
        return code_id;
    }
    case CIRTOK_LPAREN: {
        // comma expression or statement expression
        CirLex__next(); // consume LPAREN
        if (cirtok.type == CIRTOK_LBRACE) {
            // statement expression
            CirEnv__pushScope();
            CirCodeId code_id = block(false);
            CirEnv__popScope();
            if (cirtok.type != CIRTOK_RPAREN)
                unexpected_token("primary_expression", "`)`");
            CirLex__next(); // consume RPAREN
            return code_id;
        } else {
            // comma expression
            CirCodeId code_id = comma_expression();
            if (cirtok.type != CIRTOK_RPAREN)
                unexpected_token("primary_expression", "`)`");
            CirLex__next(); // consume RPAREN
            return code_id;
        }
    }
    default:
        unexpected_token("primary_expression", "INTLIT, STRINGLIT, IDENT, `(`");
    }
}

static CirCodeId
postfix_expression(void)
{
    CirCodeId lhs_id;

    lhs_id = primary_expression();
loop:
    switch (cirtok.type) {
    case CIRTOK_DOT: {
        CirArray(CirName) fields = CIRARRAY_INIT;

        while (cirtok.type == CIRTOK_DOT) {
            CirLex__next(); // consume dot
            if (cirtok.type != CIRTOK_IDENT && cirtok.type != CIRTOK_TYPENAME)
                unexpected_token("dot", "`IDENT`, `TYPENAME`");
            CirArray_push(&fields, &cirtok.data.name);
            CirLex__next();
        }
        assert(fields.len > 0);

        const CirValue *value = CirCode_getValue(lhs_id);
        if (!value)
            cir_fatal("dot: operand has no value");
        const CirType *castType = CirValue_getCastType(value);
        if (castType) {
            // Save value to temporary with the type of the cast
            CirVarId tempVar = CirVar_new(lhs_id);
            CirVar_setType(tempVar, castType);
            CirStmtId assign_stmt_id = CirCode_appendNewStmt(lhs_id);
            const CirValue *newValue = CirValue_ofVar(tempVar);
            CirStmt_toUnOp(assign_stmt_id, newValue, CIR_UNOP_IDENTITY, value);
            value = newValue;
        }
        CirCode_setValue(lhs_id, CirValue_withFields(value, fields.items, fields.len));
        CirArray_release(&fields);
        goto loop;
    }
    case CIRTOK_ARROW: {
        CirLex__next(); // consume arrow
        if (cirtok.type != CIRTOK_IDENT && cirtok.type != CIRTOK_TYPENAME)
            unexpected_token("arrow", "`IDENT`, `TYPENAME`");
        const CirValue *value = CirCode_getValue(lhs_id);
        if (CirValue_isVar(value) && !CirValue_getNumFields(value) && !CirValue_getCastType(value)) {
            // Convert to mem
            value = CirValue_ofMem(CirValue_getVar(value));
            value = CirValue_withFields(value, &cirtok.data.name, 1);
        } else {
            // Save pointer to temporary, then create a mem.
            CirVarId tempVar = CirVar_new(lhs_id);
            CirVar_setType(tempVar, CirValue_getType(value));
            CirStmtId assign_stmt_id = CirCode_appendNewStmt(lhs_id);
            CirStmt_toUnOp(assign_stmt_id, CirValue_ofVar(tempVar), CIR_UNOP_IDENTITY, value);
            value = CirValue_ofMem(tempVar);
            value = CirValue_withFields(value, &cirtok.data.name, 1);
        }
        CirCode_setValue(lhs_id, value);
        CirLex__next();
        goto loop;
    }
    case CIRTOK_LBRACKET: { // array subscript
        CirLex__next(); // consume LBRACKET
        CirCodeId rhs_id = comma_expression();
        if (cirtok.type != CIRTOK_RBRACKET)
            unexpected_token("array subscript", "`]`");
        CirLex__next(); // consume RBRACKET
        lhs_id = CirBuild__arraySubscript(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    }
    case CIRTOK_LPAREN: {
        // function call
        CirLex__next();

        // Collect function arguments.
        CirArray(CirCodeId) args = CIRARRAY_INIT;

        // Are we calling with any arguments?
        if (cirtok.type == CIRTOK_RPAREN) {
            CirLex__next();
            goto build_function_call;
        }

        for (;;) {
            CirCodeId argCode = expression();
            assert(CirCode_isExpr(argCode));
            CirArray_push(&args, &argCode);
            if (cirtok.type == CIRTOK_COMMA) {
                CirLex__next();
            } else if (cirtok.type == CIRTOK_RPAREN) {
                CirLex__next();
                break;
            } else {
                unexpected_token("function_call", "`,`, `)`");
            }
        }
build_function_call:
        lhs_id = CirBuild__call(lhs_id, args.items, args.len, CirParse__mach);
        CirArray_release(&args);
        goto loop;
    }
    default:
        return lhs_id;
    }
}

static CirCodeId
unary_expression(void)
{
    switch (cirtok.type) {
    case CIRTOK_SIZEOF: {
        CirLex__next();
        const CirType *t;
        if (cirtok.type == CIRTOK_LPAREN) {
            // Might be type_name or comma_expression
            CirLex__next();
            if (decl_spec_list_FIRST()) {
                // Is type_name
                t = type_name(CIRTOK_RPAREN);
            } else {
                // Is comma_expression
                CirCodeId code_id = comma_expression();
                t = CirCode_getType(code_id);
                CirCode_free(code_id);
            }
            if (cirtok.type != CIRTOK_RPAREN)
                cir_fatal("sizeof: expected RPAREN");
            CirLex__next();
        } else {
            // Is unary_expression
            CirCodeId code_id = unary_expression();
            t = CirCode_getType(code_id);
            CirCode_free(code_id);
        }
        uint64_t size = CirType_sizeof(t, CirParse__mach);
        uint32_t ikind = CirIkind_fromSize(CirParse__mach->sizeofSizeT, true, CirParse__mach);
        return CirCode_ofExpr(CirValue_ofU64(ikind, size));
    }
    case CIRTOK_EXCLAM: { // NOT
        CirLex__next();
        CirCodeId code_id = unary_expression();
        return CirBuild__lnot(code_id);
    }
    default:
        return postfix_expression();
    }
}

static CirCodeId
cast_expression(void)
{
    if (cirtok.type == CIRTOK_LPAREN) {
        // cast or comma expression or statement expression
        CirLex__next(); // consume LPAREN
        if (decl_spec_list_FIRST()) {
            // cast
            const CirType *type = type_name(CIRTOK_RPAREN);
            if (cirtok.type != CIRTOK_RPAREN)
                unexpected_token("cast_expression", "`)`");
            CirLex__next(); // consume RPAREN
            CirCodeId code_id = cast_expression();
            code_id = CirCode_toExpr(code_id, false);
            const CirValue *value = CirCode_getValue(code_id);
            if (!value)
                cir_fatal("cast_expression: rhs has no value");
            value = CirValue_withCastType(value, type);
            CirCode_setValue(code_id, value);
            return code_id;
        } else if (cirtok.type == CIRTOK_LBRACE) {
            // statement expression
            CirEnv__pushScope();
            CirCodeId code_id = block(false);
            CirEnv__popScope();
            if (cirtok.type != CIRTOK_RPAREN)
                unexpected_token("cast_expression", "`)`");
            CirLex__next(); // consume RPAREN
            return code_id;
        } else {
            // comma expression
            CirCodeId code_id = comma_expression();
            if (cirtok.type != CIRTOK_RPAREN)
                unexpected_token("cast_expression", "`)`");
            CirLex__next(); // consume RPAREN
            return code_id;
        }
    } else {
        return unary_expression();
    }
}

static CirCodeId
multiplicative_expression(void)
{
    CirCodeId lhs_id, rhs_id;
    lhs_id = cast_expression();
loop:
    switch (cirtok.type) {
    case CIRTOK_STAR: // mul
        CirLex__next();
        rhs_id = cast_expression();
        lhs_id = CirBuild__mul(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_SLASH: // div
        CirLex__next();
        rhs_id = cast_expression();
        lhs_id = CirBuild__div(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    default:
        return lhs_id;
    }
}

static CirCodeId
additive_expression(void)
{
    CirCodeId lhs_id, rhs_id;
    lhs_id = multiplicative_expression();
loop:
    switch (cirtok.type) {
    case CIRTOK_PLUS: // add
        CirLex__next();
        rhs_id = multiplicative_expression();
        lhs_id = CirBuild__plus(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_MINUS: // sub
        CirLex__next();
        rhs_id = multiplicative_expression();
        lhs_id = CirBuild__minus(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    default:
        return lhs_id;
    }
    return multiplicative_expression();
}

static CirCodeId
shift_expression(void)
{
    return additive_expression();
}

static CirCodeId
relational_expression(void)
{
    CirCodeId lhs_id, rhs_id;
    lhs_id = shift_expression();
loop:
    switch (cirtok.type) {
    case CIRTOK_INF: // <
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__lt(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_SUP: // >
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__gt(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_INF_EQ: // <=
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__le(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_SUP_EQ: // >=
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__gt(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_EQ_EQ: // ==
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__eq(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    case CIRTOK_EXCLAM_EQ: // !=
        CirLex__next();
        rhs_id = shift_expression();
        lhs_id = CirBuild__ne(lhs_id, rhs_id, CirParse__mach);
        goto loop;
    default:
        return lhs_id;
    }
}

static CirCodeId
equality_expression(void)
{
    return relational_expression();
}

static CirCodeId
bitwise_and_expression(void)
{
    return equality_expression();
}

static CirCodeId
bitwise_xor_expression(void)
{
    return bitwise_and_expression();
}

static CirCodeId
bitwise_or_expression(void)
{
    return bitwise_xor_expression();
}

static CirCodeId
logical_and_expression(void)
{
    return bitwise_or_expression();
}

static CirCodeId
logical_or_expression(void)
{
    return logical_and_expression();
}

static CirCodeId
conditional_expression(void)
{
    CirCodeId lhs_id = logical_or_expression();
    if (cirtok.type == CIRTOK_QUEST) {
        // Is ternary operator
        CirLex__next();
        CirCodeId thenCode = comma_expression();
        thenCode = CirCode_toExpr(thenCode, false);
        const CirValue *thenValue = CirCode_getValue(thenCode);
        if (!thenValue)
            cir_fatal("ternary: thencode has no value");
        const CirType *thenType = CirCode_getType(thenCode);
        thenType = CirType__arrayToPtr(thenType);
        if (cirtok.type != CIRTOK_COLON)
            unexpected_token("ternary", "`:`");
        CirLex__next();
        CirCodeId elseCode = conditional_expression();
        elseCode = CirCode_toExpr(elseCode, false);
        const CirValue *elseValue = CirCode_getValue(elseCode);
        if (!elseValue)
            cir_fatal("ternary: else branch has no value");
        CirVarId tmpvar_id = CirVar_new(lhs_id);
        // TODO: Check to see if then and else have the same types
        CirVar_setType(tmpvar_id, thenType);
        const CirValue *tmpvar_value = CirValue_ofVar(tmpvar_id);
        CirStmtId stmt_id;
        stmt_id = CirCode_appendNewStmt(thenCode);
        CirStmt_toUnOp(stmt_id, tmpvar_value, CIR_UNOP_IDENTITY, thenValue);
        stmt_id = CirCode_appendNewStmt(elseCode);
        CirStmt_toUnOp(stmt_id, tmpvar_value, CIR_UNOP_IDENTITY, elseValue);
        lhs_id = CirBuild__if(lhs_id, thenCode, elseCode);
        assert(CirCode_isExpr(lhs_id));
        CirCode_setValue(lhs_id, tmpvar_value);
    }
    return lhs_id;
}

static CirCodeId
assignment_expression(void)
{
    CirCodeId lhs_id, rhs_id;
    lhs_id = conditional_expression();
    switch (cirtok.type) {
    case CIRTOK_EQ: // =
        CirLex__next();
        rhs_id = assignment_expression();
        lhs_id = CirBuild__simpleAssign(lhs_id, rhs_id, CirParse__mach);
        return lhs_id;
    default:
        return lhs_id;
    }
}

static CirCodeId
expression(void)
{
    return assignment_expression();
}

// Post-condition: returned CirCodeId may be a expr or cond
static CirCodeId
comma_expression(void)
{
    CirCodeId code = expression();
    while (cirtok.type == CIRTOK_COMMA) {
        CirLex__next(); // consume COMMA
        code = CirCode_toExpr(code, true);
        CirCodeId code2 = expression();
        CirCode_append(code, code2);
    }
    return code;
}

// Pre/Post-condition: !blockCode || CirCode_isExpr(blockCode)
static CirCodeId
statement(CirCodeId blockCode, bool dropValue)
{
    assert(!blockCode || CirCode_isExpr(blockCode));

    switch (cirtok.type) {
    case CIRTOK_SEMICOLON:
        // Empty statement
        CirLex__next();
        return blockCode;
    case CIRTOK_LBRACE: {
        // Nested block
        CirEnv__pushScope();
        CirCodeId nestedBlock = block(dropValue);
        assert(!nestedBlock || CirCode_isExpr(nestedBlock));
        CirEnv__popScope();
        if (!blockCode)
            blockCode = nestedBlock;
        else if (nestedBlock)
            CirCode_append(blockCode, nestedBlock);
        assert(!blockCode || CirCode_isExpr(blockCode));
        return blockCode;
    }
    case CIRTOK_RETURN: {
        // return
        CirLex__next();
        if (cirtok.type == CIRTOK_SEMICOLON) {
            // No return value
            if (!blockCode)
                blockCode = CirCode_ofExpr(NULL);
            CirStmtId return_stmt_id = CirCode_appendNewStmt(blockCode);
            CirStmt_toReturn(return_stmt_id, NULL);
            return blockCode;
        }
        CirCodeId exprCode = comma_expression();
        exprCode = CirCode_toExpr(exprCode, false);
        if (cirtok.type != CIRTOK_SEMICOLON)
            unexpected_token("block_return_expression", "`;`");
        CirLex__next();
        const CirValue *returnValue = CirCode_getValue(exprCode);
        if (!blockCode)
            blockCode = exprCode;
        else
            CirCode_append(blockCode, exprCode);
        CirStmtId return_stmt_id = CirCode_appendNewStmt(blockCode);
        CirStmt_toReturn(return_stmt_id, returnValue);
        assert(CirCode_isExpr(blockCode));
        return blockCode;
    }
    case CIRTOK_IF: {
        // If statement
        CirLex__next();
        if (cirtok.type != CIRTOK_LPAREN)
            unexpected_token("if", "`(`");
        CirLex__next(); // consume LPAREN
        CirCodeId condCode = comma_expression();
        if (cirtok.type != CIRTOK_RPAREN)
            unexpected_token("if", "`)`");
        CirLex__next(); // consume RPAREN
        if (!blockCode)
            blockCode = condCode;
        else
            CirCode_append(blockCode, condCode);
        CirCodeId thenCode = statement(0, true);
        CirCodeId elseCode = 0;
        if (cirtok.type == CIRTOK_ELSE) {
            CirLex__next(); // consume ELSE
            elseCode = statement(0, true);
        }
        blockCode = CirBuild__if(blockCode, thenCode, elseCode);
        assert(CirCode_isExpr(blockCode));
        return blockCode;
    }
    case CIRTOK_WHILE: {
        // While statement
        CirLex__next();
        if (cirtok.type != CIRTOK_LPAREN)
            unexpected_token("while", "`(`");
        CirLex__next(); // consume LPAREN
        CirCodeId condCode = comma_expression();
        CirStmtId firstStmt = CirCode_getFirstStmt(condCode);
        if (cirtok.type != CIRTOK_RPAREN)
            unexpected_token("while", "`)`");
        CirLex__next(); // consume RPAREN
        if (!blockCode)
            blockCode = condCode;
        else
            CirCode_append(blockCode, condCode);
        CirCodeId thenCode = statement(0, true);
        blockCode = CirBuild__while(blockCode, firstStmt, thenCode);
        assert(CirCode_isExpr(blockCode));
        return blockCode;
    }
    default: {
        // comma_expression
        CirCodeId exprCode = comma_expression();
        exprCode = CirCode_toExpr(exprCode, dropValue);
        if (cirtok.type != CIRTOK_SEMICOLON)
            unexpected_token("block_expression", "`;`");
        CirLex__next();
        if (!blockCode)
            blockCode = exprCode;
        else
            CirCode_append(blockCode, exprCode);
        assert(CirCode_isExpr(blockCode));
        return blockCode;
    }
    }
}

// Post-condition: !blockCode || CirCode_isExpr(blockCode)
static CirCodeId
block(bool dropValue)
{
    if (cirtok.type != CIRTOK_LBRACE)
        unexpected_token("block", "`{`");
    CirLex__next();
    CirCodeId blockCode = 0;
    while (cirtok.type != CIRTOK_RBRACE) {
        // statements and declarations in a block, in any order
        if (decl_spec_list_FIRST()) {
            if (!blockCode)
                blockCode = CirCode_ofExpr(NULL);
            declaration_or_function_definition(blockCode);
        } else {
            blockCode = statement(blockCode, dropValue);
        }
    }
    assert(cirtok.type == CIRTOK_RBRACE);
    CirLex__next();
    assert(!blockCode || CirCode_isExpr(blockCode));
    return blockCode;
}

static bool
decl_spec_list_FIRST(void)
{
    return cirtok.type == CIRTOK_TYPEDEF ||
        cirtok.type == CIRTOK_EXTERN ||
        cirtok.type == CIRTOK_STATIC ||
        cirtok.type == CIRTOK_AUTO ||
        cirtok.type == CIRTOK_REGISTER ||
        // cvspec
        cirtok.type == CIRTOK_CONST ||
        cirtok.type == CIRTOK_VOLATILE ||
        cirtok.type == CIRTOK_RESTRICT ||
        // typespecs
        cirtok.type == CIRTOK_VOID ||
        cirtok.type == CIRTOK_CHAR ||
        cirtok.type == CIRTOK_BOOL ||
        cirtok.type == CIRTOK_SHORT ||
        cirtok.type == CIRTOK_INT ||
        cirtok.type == CIRTOK_LONG ||
        cirtok.type == CIRTOK_FLOAT ||
        cirtok.type == CIRTOK_DOUBLE ||
        cirtok.type == CIRTOK_SIGNED ||
        cirtok.type == CIRTOK_UNSIGNED ||
        cirtok.type == CIRTOK_TYPENAME ||
        cirtok.type == CIRTOK_AUTO_TYPE ||
        cirtok.type == CIRTOK_STRUCT ||
        cirtok.type == CIRTOK_UNION ||
        // attribute_nocv
        attribute_list_FIRST(false, false);
}

static int
decl_spec_list_sortTypeSpecItem(const void* aa, const void *bb)
{
    const CirParse__TypeSpecItem *a = aa, *b = bb;
    int aRank, bRank;
    switch (a->type) {
    case CIRPARSE_TVOID: aRank = 0; break;
    case CIRPARSE_TSIGNED: aRank = 1; break;
    case CIRPARSE_TUNSIGNED: aRank = 2; break;
    case CIRPARSE_TCHAR: aRank = 3; break;
    case CIRPARSE_TSHORT: aRank = 4; break;
    case CIRPARSE_TLONG: aRank = 5; break;
    case CIRPARSE_TINT: aRank = 6; break;
    case CIRPARSE_TFLOAT: aRank = 8; break;
    case CIRPARSE_TDOUBLE: aRank = 9; break;
    default: aRank = 10; break;
    }
    switch (b->type) {
    case CIRPARSE_TVOID: bRank = 0; break;
    case CIRPARSE_TSIGNED: bRank = 1; break;
    case CIRPARSE_TUNSIGNED: bRank = 2; break;
    case CIRPARSE_TCHAR: bRank = 3; break;
    case CIRPARSE_TSHORT: bRank = 4; break;
    case CIRPARSE_TLONG: bRank = 5; break;
    case CIRPARSE_TINT: bRank = 6; break;
    case CIRPARSE_TFLOAT: bRank = 8; break;
    case CIRPARSE_TDOUBLE: bRank = 9; break;
    default: bRank = 10; break;
    }
    return aRank - bRank;
}

static CirCompId
declareComp(CirName name, bool isStruct)
{
    CirCompId cid;
    int result = name ? CirEnv__findLocalTag(name, &cid) : 0;
    if (CirEnv__findLocalTag(name, &cid) == 1) {
        bool res = CirComp_isStruct(cid);
        if (res == isStruct) {
            // All OK
            return cid;
        } else if (res) {
            cir_fatal("already declared as a struct: %s", CirName_cstr(name));
        } else {
            cir_fatal("already declared as a union: %s", CirName_cstr(name));
        }
    } else if (!result) {
        // Not declared as anything, make a forward declaration
        cid = CirComp_new();
        CirComp_setStruct(cid, isStruct);
        CirComp_setName(cid, name);
        if (name)
            CirEnv__setLocalTagAsComp(cid);
        return cid;
    } else {
        cir_fatal("declared as a different tag: %s", CirName_cstr(name));
    }
}

static void
decl_spec_list(CirParse__ProcessedSpec *pspec)
{
    CirParse__TypeSpecArray typeSpecs = CIRARRAY_INIT;
    CirParse__TypeSpecItem typeSpec;
    pspec->storage = CIR_NOSTORAGE;
    pspec->isInline = false;
    pspec->isTypedef = false;
    bool seenTypeName = false;
    bool seenStorage = false;
    bool isStruct;

    assert(decl_spec_list_FIRST());

loop:
    isStruct = false;
    switch (cirtok.type) {
    // typedef
    case CIRTOK_TYPEDEF:
        CirLex__next();
        pspec->isTypedef = true;
        goto loop;

    // attr
    case CIRTOK_ATTRIBUTE:
        attribute_list(&pspec->attrArray, false, false);
        goto loop;

    // storage
    case CIRTOK_EXTERN:
        CirLex__next();
        if (seenStorage)
            cir_fatal("multiple storage specifiers");
        pspec->storage = CIR_EXTERN;
        seenStorage = true;
        goto loop;
    case CIRTOK_STATIC:
        CirLex__next();
        if (seenStorage)
            cir_fatal("multiple storage specifiers");
        pspec->storage = CIR_STATIC;
        seenStorage = true;
        goto loop;
    case CIRTOK_AUTO:
        CirLex__next();
        if (seenStorage)
            cir_fatal("multiple storage specifiers");
        pspec->storage = CIR_NOSTORAGE;
        seenStorage = true;
        goto loop;
    case CIRTOK_REGISTER:
        CirLex__next();
        if (seenStorage)
            cir_fatal("multiple storage specifiers");
        pspec->storage = CIR_REGISTER;
        seenStorage = true;
        goto loop;

    // inline
    case CIRTOK_INLINE:
        CirLex__next();
        pspec->isInline = true;
        goto loop;

    // cvspecs
    case CIRTOK_CONST:
        CirLex__next();
        // TODO
        goto loop;
    case CIRTOK_VOLATILE:
        CirLex__next();
        // TODO
        goto loop;
    case CIRTOK_RESTRICT:
        CirLex__next();
        // TODO
        goto loop;

    // typespecs
    case CIRTOK_VOID:
        CirLex__next();
        typeSpec.type = CIRPARSE_TVOID;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_CHAR:
        CirLex__next();
        typeSpec.type = CIRPARSE_TCHAR;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_BOOL:
        CirLex__next();
        typeSpec.type = CIRPARSE_TBOOL;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_SHORT:
        CirLex__next();
        typeSpec.type = CIRPARSE_TSHORT;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_INT:
        CirLex__next();
        typeSpec.type = CIRPARSE_TINT;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_LONG:
        CirLex__next();
        typeSpec.type = CIRPARSE_TLONG;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_FLOAT:
        CirLex__next();
        typeSpec.type = CIRPARSE_TFLOAT;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_DOUBLE:
        CirLex__next();
        typeSpec.type = CIRPARSE_TDOUBLE;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_SIGNED:
        CirLex__next();
        typeSpec.type = CIRPARSE_TSIGNED;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_UNSIGNED:
        CirLex__next();
        typeSpec.type = CIRPARSE_TUNSIGNED;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_AUTO_TYPE:
        CirLex__next();
        typeSpec.type = CIRPARSE_TAUTOTYPE;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_TYPENAME:
        if (seenTypeName)
            goto finish; // We have already seen a type name, so this is the declarator
        CirTypedefId tid;
        CirVarId vid;
        if (CirEnv__findLocalName(cirtok.data.name, &vid, &tid) != 2)
            cir_bug("env not in sync with lexer!");
        CirLex__next();
        typeSpec.type = CIRPARSE_TNAMED;
        typeSpec.data.tid = tid;
        seenTypeName = true;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_BUILTIN_VA_LIST:
        CirLex__next();
        typeSpec.type = CIRPARSE_TBUILTIN_VA_LIST;
        CirArray_push(&typeSpecs, &typeSpec);
        goto loop;
    case CIRTOK_STRUCT:
        isStruct = true;
        // Fallthrough
    case CIRTOK_UNION: {
        CirLex__next();
        CirName name = 0;
        if (cirtok.type == CIRTOK_IDENT || cirtok.type == CIRTOK_TYPENAME) {
            name = cirtok.data.name;
            CirLex__next();
        }

        if (cirtok.type == CIRTOK_LBRACE) {
            // struct/union definition with/without name
            CirLex__next(); // consume LBRACE
            CirCompId cid = declareComp(name, isStruct);
            if (CirComp_isDefined(cid))
                cir_fatal("comp has already been defined: %s", CirName_cstr(name));
            CirComp_setDefined(cid, true);
            while (cirtok.type != CIRTOK_RBRACE) {
                comp_field_declaration(cid);
            }
            assert(cirtok.type == CIRTOK_RBRACE);
            CirLex__next(); // consume RBRACE
            typeSpec.type = CIRPARSE_TCOMP;
            typeSpec.data.cid = cid;
            CirArray_push(&typeSpecs, &typeSpec);
            goto loop;
        } else if (name) {
            // struct/union declaration with name
            typeSpec.type = CIRPARSE_TCOMP;
            typeSpec.data.cid = declareComp(name, isStruct);
            CirArray_push(&typeSpecs, &typeSpec);
            goto loop;
        } else {
            cir_fatal("struct/union declaration without name");
        }
    }
    default: // unknown token
        goto finish;
    }

finish:
    // Sort type specifiers
    qsort(typeSpecs.items, typeSpecs.len, sizeof(typeSpecs.items[0]), decl_spec_list_sortTypeSpecItem);

    // Now try to make sense of it
#define C1(a) (typeSpecs.len == 1 && typeSpecs.items[0].type == (a))
#define C2(a, b) (typeSpecs.len == 2 && typeSpecs.items[0].type == (a) && typeSpecs.items[1].type == (b))
#define C3(a, b, c) (typeSpecs.len == 3 && typeSpecs.items[0].type == (a) && typeSpecs.items[1].type == (b) && typeSpecs.items[2].type == (c))
#define C4(a, b, c, d) (typeSpecs.len == 4 && typeSpecs.items[0].type == (a) && typeSpecs.items[1].type == (b) && typeSpecs.items[2].type == (c) && typeSpecs.items[3].type == (d))

    if (C1(CIRPARSE_TVOID)) {
        pspec->baseType = CirType_void();
    } else if (C1(CIRPARSE_TCHAR)) {
        pspec->baseType = CirType_int(CIR_ICHAR);
    } else if (C1(CIRPARSE_TBOOL)) {
        pspec->baseType = CirType_int(CIR_IBOOL);
    } else if (C2(CIRPARSE_TSIGNED, CIRPARSE_TCHAR)) {
        pspec->baseType = CirType_int(CIR_ISCHAR);
    } else if (C2(CIRPARSE_TUNSIGNED, CIRPARSE_TCHAR)) {
        pspec->baseType = CirType_int(CIR_IUCHAR);
    } else if (C1(CIRPARSE_TSHORT)
                || C2(CIRPARSE_TSIGNED, CIRPARSE_TSHORT)
                || C2(CIRPARSE_TSHORT, CIRPARSE_TINT)
                || C3(CIRPARSE_TSIGNED, CIRPARSE_TSHORT, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_ISHORT);
    } else if (C2(CIRPARSE_TUNSIGNED, CIRPARSE_TSHORT)
                || C3(CIRPARSE_TUNSIGNED, CIRPARSE_TSHORT, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_IUSHORT);
    } else if (typeSpecs.len == 0
                || C1(CIRPARSE_TINT)
                || C1(CIRPARSE_TSIGNED)
                || C2(CIRPARSE_TSIGNED, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_IINT);
    } else if (C1(CIRPARSE_TUNSIGNED)
                || C2(CIRPARSE_TUNSIGNED, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_IUINT);
    } else if (C1(CIRPARSE_TLONG)
                || C2(CIRPARSE_TSIGNED, CIRPARSE_TLONG)
                || C2(CIRPARSE_TLONG, CIRPARSE_TINT)
                || C3(CIRPARSE_TSIGNED, CIRPARSE_TLONG, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_ILONG);
    } else if (C2(CIRPARSE_TUNSIGNED, CIRPARSE_TLONG)
                || C3(CIRPARSE_TUNSIGNED, CIRPARSE_TLONG, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_IULONG);
    } else if (C2(CIRPARSE_TLONG, CIRPARSE_TLONG)
                || C3(CIRPARSE_TSIGNED, CIRPARSE_TLONG, CIRPARSE_TLONG)
                || C3(CIRPARSE_TLONG, CIRPARSE_TLONG, CIRPARSE_TINT)
                || C4(CIRPARSE_TSIGNED, CIRPARSE_TLONG, CIRPARSE_TLONG, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_ILONGLONG);
    } else if (C3(CIRPARSE_TUNSIGNED, CIRPARSE_TLONG, CIRPARSE_TLONG)
                || C4(CIRPARSE_TUNSIGNED, CIRPARSE_TLONG, CIRPARSE_TLONG, CIRPARSE_TINT)) {
        pspec->baseType = CirType_int(CIR_IULONGLONG);
    } else if (C1(CIRPARSE_TFLOAT)) {
        pspec->baseType = CirType_float(CIR_FFLOAT);
    } else if (C1(CIRPARSE_TDOUBLE)) {
        pspec->baseType = CirType_float(CIR_FDOUBLE);
    } else if (C2(CIRPARSE_TLONG, CIRPARSE_TDOUBLE)) {
        pspec->baseType = CirType_float(CIR_FLONGDOUBLE);
    } else if (C1(CIRPARSE_TNAMED)) {
        pspec->baseType = CirType_typedef(typeSpecs.items[0].data.tid);
    } else if (C1(CIRPARSE_TCOMP)) {
        pspec->baseType = CirType_comp(typeSpecs.items[0].data.cid);
    } else if (C1(CIRPARSE_TAUTOTYPE)) {
        pspec->baseType = NULL;
    } else if (C1(CIRPARSE_TBUILTIN_VA_LIST)) {
        pspec->baseType = CirType_valist();
    } else {
        cir_fatal("invalid combination of type specifiers");
    }

#undef C1
#undef C2
#undef C3
#undef C4
}

static void declarator(CirName *, CirParse__DeclArray *, CirAttrArray *, int);
static const CirType *doType(bool, const CirType *, const CirParse__DeclArray *);

// Returns isva
static bool
parameter_list(CirFunParamArray *outArr)
{
    bool isva = false;

    if (cirtok.type != CIRTOK_LPAREN)
        cir_fatal("parameter_list: expected `(`");
    CirLex__next();

    CirEnv__pushScope();

    if (cirtok.type == CIRTOK_RPAREN)
        goto finish;

    for (;;) {
        if (cirtok.type == CIRTOK_ELLIPSIS) {
            isva = true;
            CirLex__next();
            goto finish;
        }

        CirFunParam item = {};

        if (!decl_spec_list_FIRST())
            cir_fatal("parameter_list: expected FIRST(decl_spec_list)");

        CirParse__ProcessedSpec pspec;
        CirParse__ProcessedSpec_init(&pspec);
        decl_spec_list(&pspec);
        if (!pspec.baseType)
            cir_fatal("parameter_list: __auto_type not allowed");
        if (pspec.isInline)
            cir_fatal("inline specifier not allowed in parameter list");
        if (pspec.storage != CIR_NOSTORAGE)
            cir_fatal("storage specifier not allowed in parameter list");

        if (cirtok.type == CIRTOK_COMMA || cirtok.type == CIRTOK_RBRACE) {
            // decl_spec_list only
            item.type = pspec.baseType;
        } else {
            // decl_spec_list declarator/abstract_declarator
            CirParse__DeclArray declArr = CIRARRAY_INIT;
            CirAttrArray declAttrs = CIRARRAY_INIT;
            declarator(&item.name, &declArr, &declAttrs, DECLARATOR_MAYBEABSTRACT);
            item.type = doType(false, pspec.baseType, &declArr);
            CirArray_release(&declAttrs);
            CirParse__DeclArray_release(&declArr);
        }

        CirArray_push(outArr, &item);
        CirParse__ProcessedSpec_release(&pspec);
        if (cirtok.type == CIRTOK_COMMA) {
            CirLex__next();
        } else if (cirtok.type == CIRTOK_RPAREN) {
            break;
        } else {
            unexpected_token("parameter_list", "`,`, `)`");
        }
    }

finish:
    CirLex__next();
    // TODO: need to save env as well
    CirEnv__popScope();

    // Check to see if (void) is the only parameter
    if (outArr->len == 1 && !outArr->items[0].name && CirType_isVoid(outArr->items[0].type) && !CirType_getNumAttrs(outArr->items[0].type)) {
        outArr->len = 0;
    }

    return isva;
}

static void
direct_decl(CirName *outName, CirParse__DeclArray *outArr, int mode)
{
    // Starts with a name or nesting
    if (mode != DECLARATOR_ABSTRACT && (cirtok.type == CIRTOK_IDENT || cirtok.type == CIRTOK_TYPENAME)) {
        *outName = cirtok.data.name;
        CirLex__next();
    } else if (cirtok.type == CIRTOK_LPAREN) {
        CirLex__next();

        CirParse__DeclItem item = {};
        item.type = CIRPARSE_DTPAREN;

        if (attribute_list_FIRST(false, true))
            attribute_list(&item.attrs, false, true);

        declarator(outName, outArr, &item.rattrs, mode);

        if (cirtok.type != CIRTOK_RPAREN)
            cir_fatal("direct_decl: expected `)`, got %s", CirLex__str(cirtok.type));
        CirLex__next();

        CirArray_push(outArr, &item);
    } else if (mode >= DECLARATOR_ABSTRACT) {
        // No name
        if (outName)
            *outName = 0;
    } else {
        unexpected_token("direct_decl", "`(`, IDENT, TYPENAME");
    }

    // Zero or more parameter lists and arrays
    for (;;) {
        if (cirtok.type == CIRTOK_LBRACKET) {
            // Array
            CirParse__DeclItem item = {};
            item.type = CIRPARSE_DTARRAY;

            CirLex__next();
            if (attribute_list_FIRST(false, true))
                attribute_list(&item.attrs, false, true);
            if (cirtok.type == CIRTOK_RBRACKET) {
                // no array len
                item.hasLen = false;
                CirLex__next();
            } else {
                CirCodeId code_id = comma_expression();
                if (CirCode_getFirstStmt(code_id))
                    cir_fatal("Array size has side effects.");
                if (!CirCode_isExpr(code_id))
                    cir_fatal("Array size is not an expression.");
                const CirValue *v = CirCode_getValue(code_id);
                if (!v)
                    cir_fatal("Array size expression has no value.");
                if (!CirValue_isInt(v))
                    cir_fatal("Array size constant is not an integer.");
                const CirType *type = CirValue_getType(v);
                uint32_t ikind;
                if (!(ikind = CirType_isInt(CirType_unroll(type))))
                    cir_fatal("Array size constant does not have integer type.");
                // Try to convert it into an arrayLen
                uint32_t arrayLen;
                if (CirIkind_isSigned(ikind, CirParse__mach)) {
                    int64_t val = CirValue_getI64(v);
                    if (val < 0)
                        cir_fatal("Array size constant cannot be negative");
                    if (val > (uint32_t)-1)
                        cir_fatal("Array size constant is too large");
                    arrayLen = val;
                } else {
                    uint64_t val = CirValue_getU64(v);
                    if (val > (uint32_t)-1)
                        cir_fatal("Array size constant is too large");
                    arrayLen = val;
                }
                if (!arrayLen)
                    cir_fatal("Array size cannot be zero");
                item.hasLen = true;
                item.arrayLen = arrayLen;

                // Closing bracket
                if (cirtok.type != CIRTOK_RBRACKET)
                    unexpected_token("direct_decl", "`]`");
                CirLex__next();
            }

            CirArray_push(outArr, &item);
        } else if (cirtok.type == CIRTOK_LPAREN) {
            // Parameter list
            CirParse__DeclItem item = {};
            item.type = CIRPARSE_DTPROTO;

            item.isva = parameter_list(&item.funParams);
            CirArray_push(outArr, &item);
        } else {
            // Finish direct_decl
            break;
        }
    }

}

static void
declarator(CirName *outName, CirParse__DeclArray *outArr, CirAttrArray *outAttrs, int mode)
{
    CirParse__DeclArray pointers = CIRARRAY_INIT;

    // pointer_opt
    while (cirtok.type == CIRTOK_STAR) {
        CirLex__next();

        CirParse__DeclItem item = {};
        item.type = CIRPARSE_DTPTR;

        // attribute?
        if (attribute_list_FIRST(false, true))
            attribute_list(&item.attrs, false, true);

        CirArray_push(&pointers, &item);
    }

    direct_decl(outName, outArr, mode);

    // attributes_with_asm
    if (attribute_list_FIRST(true, true))
        attribute_list(outAttrs, true, true);

    // Apply pointers (backwards)
    for (size_t i = 0; i < pointers.len; i++) {
        CirArray_push(outArr, &pointers.items[pointers.len - i - 1]);
    }
    CirArray_release(&pointers);
}

static const CirType *
doType(bool for_typedef, const CirType *bt, const CirParse__DeclArray *declArr)
{
    struct PartitionedAttributes {
        CirAttrArray nameAttrs;
        CirAttrArray funAttrs;
        CirAttrArray typeAttrs;
        CirAttrArray nameAttrsR;
        CirAttrArray funAttrsR;
        CirAttrArray typeAttrsR;
    };
    CirArray(struct PartitionedAttributes) partitionedAttributes = CIRARRAY_INIT;

    // Partition attributes for all decl items
    CirArray_alloc(&partitionedAttributes, declArr->len);
    assert(partitionedAttributes.alloc >= declArr->len);
    for (size_t i = 0; i < declArr->len; i++) {
        CirArray_init(&partitionedAttributes.items[i].nameAttrs);
        CirArray_init(&partitionedAttributes.items[i].funAttrs);
        CirArray_init(&partitionedAttributes.items[i].typeAttrs);
        CirArray_init(&partitionedAttributes.items[i].nameAttrsR);
        CirArray_init(&partitionedAttributes.items[i].funAttrsR);
        CirArray_init(&partitionedAttributes.items[i].typeAttrsR);
        CirAttr__partition(declArr->items[i].attrs.items, declArr->items[i].attrs.len,
            &partitionedAttributes.items[i].nameAttrs,
            &partitionedAttributes.items[i].funAttrs,
            &partitionedAttributes.items[i].typeAttrs,
            CIRATTR_PARTITION_DEFAULT_TYPE);
        CirAttr__partition(declArr->items[i].rattrs.items, declArr->items[i].rattrs.len,
            &partitionedAttributes.items[i].nameAttrsR,
            &partitionedAttributes.items[i].funAttrsR,
            &partitionedAttributes.items[i].typeAttrsR,
            for_typedef ? CIRATTR_PARTITION_DEFAULT_TYPE : CIRATTR_PARTITION_DEFAULT_NAME);
    }
    partitionedAttributes.len = declArr->len;

    // Do from back to front
    for (size_t i = 0; i < declArr->len; i++) {
        const CirParse__DeclItem *item = &declArr->items[declArr->len - i - 1];
        switch (item->type) {
        case CIRPARSE_DTPAREN:
            cir_bug("TODO: handle CIRPARSE_DTPAREN");
            break;
        case CIRPARSE_DTPTR:
            bt = CirType__ptr(bt, item->attrs.items, item->attrs.len);
            break;
        case CIRPARSE_DTARRAY:
            if (item->hasLen)
                bt = CirType__arrayWithLen(bt, item->arrayLen, item->attrs.items, item->attrs.len);
            else
                bt = CirType__array(bt, item->attrs.items, item->attrs.len);
            break;
        case CIRPARSE_DTPROTO: {
            const CirType *returnType = CirType_unroll(bt);
            if (CirType_isArray(returnType)) {
                bt = CirType__arrayToPtr(returnType);
            }
            // Convert [] types in params to pointers
            for (size_t i = 0; i < item->funParams.len; i++) {
                const CirType *t = item->funParams.items[i].type;
                t = CirType_unroll(t);
                if (CirType_isArray(t)) {
                    item->funParams.items[i].type = CirType__arrayToPtr(t);
                }
            }
            bt = CirType_fun(bt, item->funParams.items, item->funParams.len, item->isva);
            break;
        }
        }
    }

    // Do from front to back
    for (size_t i = 0; i < declArr->len; i++) {
        const CirParse__DeclItem *item = &declArr->items[i];
        switch (item->type) {
        case CIRPARSE_DTPTR:
            // See if we can do anything with function type attributes
            if (!partitionedAttributes.items[i].funAttrs.len)
                break;

            if (CirType_isFun(bt)) {
                bt = CirType_withAttrs(bt, partitionedAttributes.items[i].funAttrs.items, partitionedAttributes.items[i].funAttrs.len);
            } else if (CirType_isPtr(bt) && CirType_isFun(CirType_getBaseType(bt))) {
                const CirType *fun = CirType_getBaseType(bt);
                fun = CirType_withAttrs(fun, partitionedAttributes.items[i].funAttrs.items, partitionedAttributes.items[i].funAttrs.len);
                bt = CirType__ptr(fun, CirType_getAttrs(bt), CirType_getNumAttrs(bt));
            } else {
                cir_fatal("Invalid position for function type attributes");
            }
            break;
        default:
            // Do nothing
            break;
        }
    }

    // Free partition list
    for (size_t i = 0; i < partitionedAttributes.len; i++) {
        CirArray_release(&partitionedAttributes.items[i].nameAttrs);
        CirArray_release(&partitionedAttributes.items[i].funAttrs);
        CirArray_release(&partitionedAttributes.items[i].typeAttrs);
        CirArray_release(&partitionedAttributes.items[i].nameAttrsR);
        CirArray_release(&partitionedAttributes.items[i].funAttrsR);
        CirArray_release(&partitionedAttributes.items[i].typeAttrsR);
    }
    CirArray_release(&partitionedAttributes);

    return bt;
}

static const CirType *
type_name(int expectedFollow)
{
    assert(decl_spec_list_FIRST());

    CirParse__ProcessedSpec pspec;
    CirParse__ProcessedSpec_init(&pspec);
    decl_spec_list(&pspec);
    if (pspec.isTypedef)
        cir_fatal("type_name: typedef not allowed");
    if (!pspec.baseType)
        cir_fatal("type_name: __auto_type not allowed");
    if (pspec.isInline)
        cir_fatal("type_name: inline not allowed");
    if (pspec.storage != CIR_NOSTORAGE)
        cir_fatal("type_name: storage specifier not allowed");

    if (cirtok.type == expectedFollow) {
        CirParse__ProcessedSpec_release(&pspec);
        return pspec.baseType; // No declarator following
    }

    CirParse__DeclArray declArr = CIRARRAY_INIT;
    CirAttrArray declAttrs = CIRARRAY_INIT;
    declarator(NULL, &declArr, &declAttrs, DECLARATOR_ABSTRACT);
    // TODO: do something with declAttrs

    const CirType *t = doType(false, pspec.baseType, &declArr);
    CirParse__DeclArray_release(&declArr);
    CirParse__ProcessedSpec_release(&pspec);
    return t;
}

static CirTypedefId
declareOneTypedef(const CirType *bt)
{
    assert(bt != NULL);
    CirName declName;
    CirParse__DeclArray declArr = CIRARRAY_INIT;
    CirAttrArray declAttrs = CIRARRAY_INIT;
    declarator(&declName, &declArr, &declAttrs, DECLARATOR_CONCRETE);
    // TODO: do something with declAttrs

    // Check for re-definition
    {
        CirVarId vid;
        CirTypedefId tid;
        if (CirEnv__findCurrentScopeName(declName, &vid, &tid) != 0)
            cir_fatal("re-declaration of %s", CirName_cstr(declName));
    }

    const CirType *t = doType(true, bt, &declArr);
    CirTypedefId tid = CirTypedef_new(declName, t);
    CirEnv__setLocalNameAsTypedef(tid);
    CirParse__DeclArray_release(&declArr);
    return tid;
}

static CirVarId
declareOneVar(const CirParse__ProcessedSpec *pspec, CirCodeId ownerCode)
{
    CirName declName;
    CirParse__DeclArray declArr = CIRARRAY_INIT;
    CirAttrArray declAttrs = CIRARRAY_INIT;
    declarator(&declName, &declArr, &declAttrs, DECLARATOR_CONCRETE);
    // TODO: do something with declAttrs

    // Check for re-definition
    if (!CirEnv__isGlobal()) {
        CirVarId vid;
        CirTypedefId tid;
        if (CirEnv__findCurrentScopeName(declName, &vid, &tid) != 0)
            cir_fatal("re-declaration of %s in local scope", CirName_cstr(declName));
    }

    const CirType *t = NULL;
    if (pspec->baseType)
        t = doType(false, pspec->baseType, &declArr);
    else if (declArr.len)
        cir_fatal("Cannot have declarator elems with __auto_type");

    CirVarId vid = CirVar_new(ownerCode);
    assert(vid != 0);
    CirVar_setName(vid, declName);
    CirVar_setType(vid, t);
    CirVar_setStorage(vid, pspec->storage);

    // We may need to re-use an existing vid
    if (CirEnv__isGlobal())
        vid = makeGlobalVar(vid);

    CirEnv__setLocalNameAsVar(vid);
    CirParse__DeclArray_release(&declArr);

#if 0
    CirLog_begin(CIRLOG_DEBUG);
    CirLog_print("declared var ");
    CirType_log(t, CirName_cstr(declName));
    CirLog_printf(" with owner %u", (unsigned)ownerCode);
    CirLog_end();
#endif
    return vid;
}

static size_t
declareOneCompField(CirCompId cid, const CirType *bt)
{
    assert(bt != NULL);

    CirName declName;
    CirParse__DeclArray declArr = CIRARRAY_INIT;
    CirAttrArray declAttrs = CIRARRAY_INIT;
    declarator(&declName, &declArr, &declAttrs, DECLARATOR_CONCRETE);
    // TODO: do something with declAttrs

    // TODO: check for same-name in comp

    const CirType *t = doType(true, bt, &declArr);
    size_t fieldIdx = CirComp_getNumFields(cid);
    CirComp_setNumFields(cid, fieldIdx + 1);
    CirComp_setFieldName(cid, fieldIdx, declName);
    CirComp_setFieldType(cid, fieldIdx, t);
    CirParse__DeclArray_release(&declArr);
    return fieldIdx;
}

static void
comp_field_declaration(CirCompId cid)
{
    CirParse__ProcessedSpec pspec;
    CirParse__ProcessedSpec_init(&pspec);
    decl_spec_list(&pspec);

    if (pspec.isTypedef)
        cir_fatal("comp_field_declaration: typedef not allowed");
    if (!pspec.baseType)
        cir_fatal("comp_field_declaration: __auto_type not allowed");
    if (pspec.isInline)
        cir_fatal("comp_field_declaration: inline not allowed");
    if (pspec.storage != CIR_NOSTORAGE)
        cir_fatal("comp_field_declaration: storage specifier not allowed");

    // Are we actually declaring anything?
    if (cirtok.type == CIRTOK_SEMICOLON) {
        CirLex__next();
        CirParse__ProcessedSpec_release(&pspec);
        return;
    }

    for (;;) {
        declareOneCompField(cid, pspec.baseType);

        if (cirtok.type == CIRTOK_COMMA) {
            // Next field
            CirLex__next();
        } else if (cirtok.type == CIRTOK_SEMICOLON) {
            // Terminate
            CirLex__next();
            break;
        } else {
            unexpected_token("struct field declaration", "`,`, `;`");
        }
    }

    CirParse__ProcessedSpec_release(&pspec);
}

static void
declaration_or_function_definition(CirCodeId ownerCode)
{
    assert(decl_spec_list_FIRST());

    CirVarId vid;

    // First parse specifier
    CirParse__ProcessedSpec pspec;
    CirParse__ProcessedSpec_init(&pspec);
    decl_spec_list(&pspec);

    // Do some checks on specifier
    if (pspec.isTypedef) {
        if (!pspec.baseType)
            cir_fatal("__auto_type not allowed in typedef");
        if (pspec.isInline)
            cir_fatal("inline specifier not allowed in typedef");
        if (pspec.storage != CIR_NOSTORAGE)
            cir_fatal("storage specifier not allowed in typedef");
    }

    // Are we actually declaring anything?
    if (cirtok.type == CIRTOK_SEMICOLON) {
        CirLex__next();
        CirParse__ProcessedSpec_release(&pspec);
        return;
    }

    // Handle typedefs specially
    if (pspec.isTypedef) {
        for (;;) {
            declareOneTypedef(pspec.baseType);

            if (cirtok.type == CIRTOK_COMMA) {
                // Next typedef
                CirLex__next();
            } else if (cirtok.type == CIRTOK_SEMICOLON) {
                CirLex__next();
                CirParse__ProcessedSpec_release(&pspec);
                return;
            } else {
                unexpected_token("typedef", "`,`, `;`");
            }
        }
    }

    // Parse the first declarator
    vid = declareOneVar(&pspec, ownerCode);

    // Are we looking at a function definition?
    if (cirtok.type == CIRTOK_LBRACE) {
        // Yes, we are.

        // Check that we are dealing with a function type
        const CirType *t = CirVar_getType(vid);
        if (!t)
            cir_fatal("__auto_type not allowd in function definition");
        if (!CirType_isFun(t))
            cir_fatal("function definition can only be used with a function type");

        CirEnv__pushScope();

        CirCodeId fun_code = CirCode_ofExpr(NULL);
        CirVar__setCode(vid, fun_code);

        // Create sformals for args and enter them
        size_t numParams = CirType_getNumParams(t);
        const CirFunParam *params = CirType_getParams(t);
        for (size_t i = 0; i < numParams; i++) {
            if (!params[i].name)
                cir_fatal("parameter with no name in function definition");
            CirVarId paramVid = CirVar_new(fun_code);
            CirVar_setName(paramVid, params[i].name);
            assert(params[i].type);
            CirVar_setType(paramVid, params[i].type);
            CirEnv__setLocalNameAsVar(paramVid);
            CirVar_setFormal(vid, i, paramVid);
        }

        CirCodeId block_code = block(true);
        CirCode_append(fun_code, block_code);
        CirEnv__popScope();
        assert(CirCode_isExpr(fun_code));
        #if 0
        CirLog_begin(CIRLOG_DEBUG);
        CirCode_dump(fun_code);
        CirLog_end();
        #endif
        return;
    } else {
        // No, we are not.
        goto declaration_loop_rest;
    }

    for (;;) {
        vid = declareOneVar(&pspec, ownerCode);

declaration_loop_rest:
        if (cirtok.type == CIRTOK_COMMA) {
            // No initializer
            CirLex__next();
        } else if (cirtok.type == CIRTOK_EQ) {
            // Initializer
            CirLex__next(); // consume EQ
            if (!ownerCode)
                cir_bug("TODO: handle initializer in global scope");
            if (cirtok.type == CIRTOK_LBRACE) {
                cir_bug("TODO: handle compound initializer");
            } else {
                CirCodeId code_id = expression();
                const CirValue *value = CirCode_getValue(code_id);
                if (!value)
                    cir_fatal("initializer expression has no value");
                const CirType *type = CirValue_getType(value);
                if (!CirVar_getType(vid))
                    CirVar_setType(vid, type);
                CirCode_append(ownerCode, code_id);
                CirStmtId stmt_id = CirCode_appendNewStmt(ownerCode);
                CirStmt_toUnOp(stmt_id, CirValue_ofVar(vid), CIR_UNOP_IDENTITY, value);
                CirCode_setValue(ownerCode, NULL);
            }

            if (cirtok.type == CIRTOK_COMMA) {
                // Continuing
                CirLex__next();
            } else if (cirtok.type == CIRTOK_SEMICOLON) {
                // Terminate
                CirLex__next();
                break;
            } else {
                unexpected_token("var declaration", "`,`, `;`");
            }
        } else if (cirtok.type == CIRTOK_SEMICOLON) {
            // No initializer, terminate
            CirLex__next();
            break;
        } else {
            unexpected_token("var declaration", "`,`, `=`, `;`");
        }
    }

    CirParse__ProcessedSpec_release(&pspec);
}

static void
toplevel(void)
{
    if (decl_spec_list_FIRST()) {
        declaration_or_function_definition(0);
    } else {
        unexpected_token("toplevel", "decl_spec_list_FIRST");
    }
}

void
cir__parse(const CirMachine *mach)
{
    CirParse__mach = mach;

    // Initialize global scope
    CirEnv__pushScope();

    CirLex__next();
    while (cirtok.type != CIRTOK_EOF)
        toplevel();
}
