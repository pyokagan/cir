#ifndef CIR_INTERNAL_H
#define CIR_INTERNAL_H
#include "cir.h"
#include <inttypes.h>
#include <stdio.h>

#define CIR_PRIVATE

extern const char CirQuote__table[256][5];

typedef void (*CirFmt)(const void *buf, size_t len);

static inline void
CirFmt_printString(CirFmt print, const char *s)
{
    print(s, strlen(s));
}

static inline void
CirFmt_printI32(CirFmt print, int32_t x)
{
    char buf[12];
    size_t len = snprintf(buf, sizeof(buf), "%" PRId32, x);
    print(buf, len);
}

static inline void
CirFmt_printU32(CirFmt print, uint32_t x)
{
    char buf[12];
    size_t len = snprintf(buf, sizeof(buf), "%" PRIu32, x);
    print(buf, len);
}

static inline void
CirFmt_printI64(CirFmt print, int64_t x)
{
    char buf[21];
    size_t len = snprintf(buf, sizeof(buf), "%" PRId64, x);
    print(buf, len);
}

static inline void
CirFmt_printU64(CirFmt print, uint64_t x)
{
    char buf[21];
    size_t len = snprintf(buf, sizeof(buf), "%" PRIu64, x);
    print(buf, len);
}

static inline void
CirFmt_printqb(CirFmt printer, const char *s, size_t len)
{
    CirFmt_printString(printer, "\"");
    for (size_t i = 0; i < len; i++) {
        uint8_t c = s[i];
        CirFmt_printString(printer, CirQuote__table[c]);
        if (c >= 127)
            CirFmt_printString(printer, "\"\"");
    }
    CirFmt_printString(printer, "\"");
}

// CirHash
size_t CirHash_str(const char *);

// Provides information on a machine
typedef struct CirMachine {
    unsigned compiler;
    unsigned sizeofShort;
    unsigned sizeofInt;
    unsigned sizeofBool;
    unsigned sizeofLong;
    unsigned sizeofLongLong;
    unsigned sizeofPtr;
    unsigned sizeofFloat;
    unsigned sizeofDouble;
    unsigned sizeofLongDouble;
    unsigned sizeofFloat128;
    unsigned sizeofVoid;
    unsigned sizeofFun;
    unsigned sizeofSizeT;
    unsigned alignofShort;
    unsigned alignofInt;
    unsigned alignofBool;
    unsigned alignofLong;
    unsigned alignofLongLong;
    unsigned alignofPtr;
    unsigned alignofEnum;
    unsigned alignofFloat;
    unsigned alignofDouble;
    unsigned alignofLongDouble;
    unsigned alignofFloat128;
    unsigned alignofFun;
    bool charIsUnsigned;
} CirMachine;
void CirMachine__initBuiltin(CirMachine *);
void CirMachine__logCompiler(uint32_t);
void CirMachine__log(const CirMachine *);
extern CirMachine CirMachine__build;
extern CirMachine CirMachine__host;

typedef struct CirToken {
    enum CirTokType {
        CIRTOK_NONE = 0,
        CIRTOK_EOF,

        // Idents
        CIRTOK_IDENT,
        CIRTOK_TYPENAME,
        CIRTOK_BUILTIN,

        // Literals
        CIRTOK_STRINGLIT,
        CIRTOK_CHARLIT,
        CIRTOK_INTLIT,

        // Operators
        CIRTOK_INF_INF_EQ, // <<=
        CIRTOK_SUP_SUP_EQ, // >>=
        CIRTOK_ELLIPSIS, // ...
        CIRTOK_PLUS_EQ, // +=
        CIRTOK_MINUS_EQ, // -=
        CIRTOK_STAR_EQ, // *=
        CIRTOK_SLASH_EQ, // /=
        CIRTOK_PERCENT_EQ, // %=
        CIRTOK_PIPE_EQ, // |=
        CIRTOK_AND_EQ, // &=
        CIRTOK_CIRC_EQ, // ^=
        CIRTOK_INF_INF, // <<
        CIRTOK_SUP_SUP, // >>
        CIRTOK_EQ_EQ, // ==
        CIRTOK_EXCLAM_EQ, // !=
        CIRTOK_INF_EQ, // <=
        CIRTOK_SUP_EQ, // >=
        CIRTOK_PLUS_PLUS, // ++
        CIRTOK_MINUS_MINUS, // --
        CIRTOK_ARROW, // ->
        CIRTOK_AND_AND, // &&
        CIRTOK_PIPE_PIPE, // ||
        CIRTOK_EQ, // =
        CIRTOK_INF, // <
        CIRTOK_SUP, // >
        CIRTOK_PLUS, // +
        CIRTOK_MINUS, // -
        CIRTOK_STAR, // *
        CIRTOK_SLASH, // /
        CIRTOK_PERCENT, // %
        CIRTOK_EXCLAM, // !
        CIRTOK_AND, // &
        CIRTOK_PIPE, // |
        CIRTOK_CIRC, // ^
        CIRTOK_QUEST, // ?
        CIRTOK_COLON, // :
        CIRTOK_TILDE, // ~
        CIRTOK_LBRACE, // {
        CIRTOK_RBRACE, // }
        CIRTOK_LBRACKET, // [
        CIRTOK_RBRACKET, // ]
        CIRTOK_LPAREN, // (
        CIRTOK_RPAREN, // )
        CIRTOK_SEMICOLON, // ;
        CIRTOK_COMMA, // ,
        CIRTOK_DOT, // .
        CIRTOK_AT, // @

        // Keywords
        CIRTOK_AUTO, // auto
        CIRTOK_CONST, // const
        CIRTOK_STATIC, // static
        CIRTOK_EXTERN, // extern
        CIRTOK_LONG, // long
        CIRTOK_SHORT, // short
        CIRTOK_REGISTER, // register
        CIRTOK_SIGNED, // signed
        CIRTOK_UNSIGNED, // unsigned
        CIRTOK_VOLATILE, // volatile
        CIRTOK_BOOL, // _Bool
        CIRTOK_CHAR, // char
        CIRTOK_INT, // int
        CIRTOK_FLOAT, // float
        CIRTOK_DOUBLE, // double
        CIRTOK_VOID, // void
        CIRTOK_ENUM, // enum
        CIRTOK_STRUCT, // struct
        CIRTOK_TYPEDEF, // typedef
        CIRTOK_UNION, // union
        CIRTOK_BREAK, // break
        CIRTOK_CONTINUE, // continue
        CIRTOK_GOTO, // goto
        CIRTOK_RETURN, // return
        CIRTOK_SWITCH, // switch
        CIRTOK_CASE, // case
        CIRTOK_DEFAULT, // default
        CIRTOK_WHILE, // while
        CIRTOK_DO, // do
        CIRTOK_FOR, // for
        CIRTOK_IF, // if
        CIRTOK_ELSE, // else
        CIRTOK_AUTO_TYPE, // __auto_type
        CIRTOK_INLINE, // inline
        CIRTOK_ATTRIBUTE, // __attribute__
        CIRTOK_ASM, // __asm__
        CIRTOK_TYPEOF, // typeof
        CIRTOK_ALIGNOF, // __alignof
        CIRTOK_RESTRICT, // restrict
        CIRTOK_BUILTIN_VA_LIST, // __builtin_va_list
        CIRTOK_SIZEOF, // sizeof
        CIRTOK_TYPEVAL, // __typeval
        CIRTOK_FLOAT128, // _Float128
    } type;
    union {
        CirName name; // CIRTOK_IDENT, CIRTOK_TYPENAME
        char charlit; // CIRTOK_CHARLIT
        struct {
            union {
                uint64_t u64;
                int64_t i64;
            } val;
            unsigned int ikind;
        } intlit; // CIRTOK_INTLIT
        struct {
            char *buf;
            size_t len;
        } stringlit; // CIRTOK_STRINGLIT
        CirBuiltinId builtinId; // CIRTOK_BUILTIN
    } data;
} CirToken;

extern CirToken cirtok;

// via malloc
void *cir__xalloc(size_t n) __attribute__((malloc));
void *cir__zalloc(size_t n) __attribute__((malloc));
void *cir__xrealloc(void *, size_t);
void cir__xfree(void *);

// Line numbering
void CirLog__announceNewLine(void);
void CirLog__pushLocation(CirName file, uint32_t line);
void CirLog__popLocation(void);
void CirLog__setLocation(CirName file, uint32_t line);
void CirLog__setRealLocation(CirName file, uint32_t line);

// CirType
const CirType *CirType__void(const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__int(uint32_t ikind, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__float(uint32_t fkind, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__typedef(CirTypedefId, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__comp(CirCompId, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__enum(CirEnumId, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__ptr(const CirType *, const CirAttr * const *attrs, size_t numAttrs);
const CirType *CirType__array(const CirType *, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__arrayWithLen(const CirType *, uint32_t len, const CirAttr * const *attrs, uint32_t numAttrs);
const CirType *CirType__fun(const CirType *, const CirFunParam *params, size_t numParams, bool isVa, const CirAttr * const *attrs, size_t numAttrs);
const CirType *CirType__valist(const CirAttr * const *attrs, size_t numAttrs);
const CirType *CirType__arrayToPtr(const CirType *);
const CirType *CirType__combine(const CirType *oldt, const CirType *t);
const CirType *CirType__integralPromotion(const CirType *, const CirMachine *);
const CirType *CirType__arithmeticConversion(const CirType *t1, const CirType *t2, const CirMachine *);
void CirType_print(CirFmt printer, const CirType *, const char *name, CirCodeId code_id, bool forRender);
const CirType *CirValue_computeTypeAndBitsOffset(const CirValue *value, uint64_t *offset, const CirMachine *mach);
uint64_t CirValue_computeBitsOffset(const CirValue *value, const CirMachine *);
const CirType *CirType_ofUnOp(uint32_t unop, const CirType *t1, const CirMachine *);
const CirType *CirType_ofBinOp(uint32_t binop, const CirType *t1, const CirType *t2, const CirMachine *);
const CirType *CirType_ofCall(const CirType *);

void CirVar__setCode(CirVarId, CirCodeId);
void CirVar__setOwner(CirVarId, CirCodeId);
const CirVarId *CirVar__getFormals(CirVarId);
void CirVar_printLval(CirFmt, CirVarId, bool forRender);
void CirVar_printDecl(CirFmt, CirVarId, bool forRender);
void CirCode__addVar(CirCodeId, CirVarId);
void CirValue_print(CirFmt, const CirValue *, bool);
void CirStmt_print(CirFmt, CirStmtId, bool);

bool CirComp__isIsomorphic(CirCompId, CirCompId);
void CirComp__markIsomorphic(CirCompId, CirCompId);
void CirComp__unmarkIsomorphic(CirCompId, CirCompId);

// Env
void CirEnv__pushScope(size_t tableSize);
void CirEnv__pushGlobalScope(void);
void CirEnv__pushLocalScope(void);
void CirEnv__popScope(void);
bool CirEnv__isGlobal(void);
int CirEnv__findLocalName(CirName, CirVarId *, CirTypedefId *, CirEnumItemId *);
int CirEnv__findGlobalName(CirName, CirVarId *, CirTypedefId *, CirEnumItemId *);
int CirEnv__findCurrentScopeName(CirName, CirVarId *, CirTypedefId *, CirEnumItemId *);
void CirEnv__setLocalNameAsVar(CirVarId);
void CirEnv__setLocalNameAsTypedef(CirTypedefId);
void CirEnv__setLocalNameAsEnumItem(CirEnumItemId);
int CirEnv__findLocalTag(CirName, CirCompId *, CirEnumId *);
void CirEnv__setLocalTagAsComp(CirCompId);
void CirEnv__setLocalTagAsEnum(CirEnumId);

// CirLoopEnv
void CirLoopEnv_pushLoop(CirStmtId continueStmtId, CirStmtId breakStmtId);
void CirLoopEnv_pushSwitch(CirStmtId breakStmtId);
void CirLoopEnv_pop(void);
CirStmtId CirLoopEnv_getContinueStmtId(void);
CirStmtId CirLoopEnv_getBreakStmtId(void);

// Lexer
void CirLex__init(const char *path, const CirMachine *);
void CirLex__next(void);
void CirLex__push(const CirToken *);
const char *CirLex__str(uint32_t);

// Parser
void cir__parse(const CirMachine *);

#define CirArray(type) struct { type *items; size_t len; size_t alloc; }
typedef CirArray(void) CirGenericArray;
#define CIRARRAY_INIT { NULL, 0, 0 }
#define CirArray_init(a) do { \
    (a)->len = 0; \
    (a)->alloc = 0; \
    (a)->items = NULL; \
} while (0)
#define CirArray_release(a) do { \
    cir__xfree((a)->items); \
    CirArray_init(a); \
} while (0)
#define CirArray_clear(a) do { \
    (a)->len = 0; \
} while (0)
void CirArray__alloc(CirGenericArray *, size_t, size_t);
#define CirArray_alloc(a, n) \
    CirArray__alloc((CirGenericArray *)(a), sizeof(*((a)->items)), n)
void CirArray__grow(CirGenericArray *, size_t, size_t);
#define CirArray_grow(a, n) \
    CirArray__grow((CirGenericArray *)(a), sizeof(*((a)->items)), n)
#define CirArray_push(a, item) do { \
    CirArray_grow(a, 1); \
    (a)->items[(a)->len++] = *(item); \
} while (0)

typedef CirArray(uint8_t) CirBBuf;
#define CIRBBUF_INIT CIRARRAY_INIT
#define CirBBuf_alloc(a, n) CirArray_alloc(a, n)
#define CirBBuf_grow(a, n) CirArray_grow(a, n)
#define CirBBuf_init(a) CirArray_init(a)
#define CirBBuf_release(a) CirArray_release(a)
void CirBBuf__add(CirBBuf *, const uint8_t *, size_t);
void CirBBuf__readFile(CirBBuf *, const char *path);

CirStmtId CirStmt__new(CirCodeId code);
void CirStmt__setNextStmt(CirStmtId sid, CirStmtId next_sid);
void CirStmt__setNextCode(CirStmtId sid, CirCodeId cid);
void CirStmt__setPrevStmt(CirStmtId sid, CirStmtId prev_sid);
void CirStmt__setPrevCode(CirStmtId sid, CirCodeId cid);
void CirCode__setLastStmt(CirCodeId, CirStmtId);
void CirCode__setFirstStmt(CirCodeId, CirStmtId);

typedef CirArray(const CirAttr *) CirAttrArray;
void CirAttrArray__add(CirAttrArray *, const CirAttr *);
void CirAttrArray__merge(CirAttrArray *, const CirAttr * const *srcA, size_t lenA, const CirAttr * const *srcB, size_t lenB);
void CirAttrArray__remove(CirAttrArray *, const CirAttr *const *srcA, size_t lenA, const CirAttr *const *removeB, size_t lenB);
#define CIRATTR_PARTITION_DEFAULT_NAME 0
#define CIRATTR_PARTITION_DEFAULT_FUN 1
#define CIRATTR_PARTITION_DEFAULT_TYPE 2
void CirAttr__partition(const CirAttr * const *attrs, size_t numAttrs, CirAttrArray *outName, CirAttrArray *outFun, CirAttrArray *outType, int _default);
void CirAttr_printArray(CirFmt printer, const CirAttr * const *attrs, size_t numAttrs);
void CirAttr__logArray(const CirAttr * const *attrs, size_t numAttrs);

CirCodeId CirBuild__mul(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__div(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__mod(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__plus(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__arraySubscript(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__minus(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__call(CirCodeId target, const CirCodeId *args, size_t numArgs, const CirMachine *);
CirCodeId CirBuild__simpleAssign(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__lt(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__le(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__gt(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__ge(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__eq(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__ne(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__land(CirCodeId lhs, CirCodeId rhs);
CirCodeId CirBuild__lor(CirCodeId lhs, CirCodeId rhs);
CirCodeId CirBuild__if(CirCodeId cond, CirCodeId thenBlock, CirCodeId elseBlock);
CirCodeId CirBuild__for(CirCodeId cond, CirStmtId firstStmt, CirCodeId thenCode, CirCodeId afterCode, CirStmtId restStmt);
CirCodeId CirBuild__lnot(CirCodeId);
CirCodeId CirBuild__addrof(CirCodeId);
CirCodeId CirBuild__deref(CirCodeId);
CirCodeId CirBuild__lshift(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__rshift(CirCodeId lhs, CirCodeId rhs, const CirMachine *);
CirCodeId CirBuild__ternary(CirCodeId cond, CirCodeId thenCode, CirCodeId elseCode, const CirMachine *mach);

size_t CirVar_getNum(void);
size_t CirComp_getNum(void);
size_t CirTypedef_getNum(void);
size_t CirStmt_getNum(void);

// Call with no arguments.
CirCodeId CirX64_call(CirVarId vid, const CirCodeId *, size_t);
void CirX64_test(void);

void CirDl_loadLibrary(const char *filename);
bool CirDl_findSymbol(const char *name, void **out);

void CirRender(void);

void CirBuiltin_init(const CirMachine *);

#endif // CIR_INTERNAL_H
