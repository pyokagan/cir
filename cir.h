#ifndef CIR_H
#define CIR_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

// Alignment boundary
#define CIR_MEM_ALIGN 8

// Maximum number of attributes
#define CIR_MAX_ATTRS 31

// Maximum number of function arguments
#define CIR_MAX_FUN_PARAMS 127

// Maximum number of nested scopes
#define CIR_MAX_SCOPES 63

// Maximum number of struct/union fields
#define CIR_MAX_FIELDS 1023

#define CIRLOG_DEBUG 1
#define CIRLOG_INFO 2
#define CIRLOG_WARN 3
#define CIRLOG_ERROR 4
#define CIRLOG_FATAL 5
#define CIRLOG_BUG 6

// Compiler
#define CIR_GCC 0
#define CIR_MSVC 1

// ikind (4 bits)
#define CIR_ICHAR 1
#define CIR_ISCHAR 2
#define CIR_IUCHAR 3
#define CIR_IBOOL 4
#define CIR_IINT 5
#define CIR_IUINT 6
#define CIR_ISHORT 7
#define CIR_IUSHORT 8
#define CIR_ILONG 9
#define CIR_IULONG 10
#define CIR_ILONGLONG 11
#define CIR_IULONGLONG 12

// fkind (2 bits)
#define CIR_FFLOAT 13
#define CIR_FDOUBLE 14
#define CIR_FLONGDOUBLE 15

// storage kind
#define CIR_NOSTORAGE 0
#define CIR_STATIC 1
#define CIR_REGISTER 2
#define CIR_EXTERN 3

// Unop
#define CIR_UNOP_NEG 1
#define CIR_UNOP_BNOT 2
#define CIR_UNOP_LNOT 3
#define CIR_UNOP_ADDROF 4
#define CIR_UNOP_IDENTITY 5

// Binop
#define CIR_BINOP_PLUS 1
#define CIR_BINOP_MINUS 2
#define CIR_BINOP_MUL 3
#define CIR_BINOP_DIV 4
#define CIR_BINOP_MOD 5
#define CIR_BINOP_SHIFTLT 6
#define CIR_BINOP_SHIFTRT 7
#define CIR_BINOP_BAND 8 // Binary &
#define CIR_BINOP_BXOR 9
#define CIR_BINOP_BOR 10

// Condop
#define CIR_CONDOP_LT 1
#define CIR_CONDOP_GT 2
#define CIR_CONDOP_LE 3
#define CIR_CONDOP_GE 4
#define CIR_CONDOP_EQ 5
#define CIR_CONDOP_NE 6

typedef struct CirMachine CirMachine;
typedef uint32_t CirName; // Reference type
typedef struct CirType CirType; // Value type
typedef uint32_t CirCompId; // Reference type
typedef uint32_t CirTypedefId; // Reference type
typedef uint32_t CirVarId; // Reference type
typedef uint32_t CirStmtId; // Reference type
typedef struct CirValue CirValue;
typedef uint32_t CirCodeId;
typedef struct CirAttr CirAttr;
typedef uint32_t CirStorage;

typedef struct CirFunParam {
    CirName name;
    const CirType *type;
} CirFunParam;

void CirLog_begin(uint32_t level);
void CirLog_print(const char *);
void CirLog_printb(const void *, size_t);
void CirLog_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void CirLog_vprintf(const char *fmt, va_list va);
void CirLog_printq(const char *);
void CirLog_printqb(const char *, size_t);
void CirLog_end(void);
void cir_fatal(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
void cir_bug(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
void cir_warn(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void cir_log(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

// CirMachine
const CirMachine *CirMachine_getBuild(void);
const CirMachine *CirMachine_getHost(void);

// CirIkind
uint32_t CirIkind_size(uint32_t ikind, const CirMachine *);
bool CirIkind_isSigned(uint32_t ikind, const CirMachine *);
uint32_t CirIkind_toUnsigned(uint32_t ikind);
uint32_t CirIkind_fromSize(uint32_t size, bool _unsigned, const CirMachine *);

// CirName
CirName CirName_of(const char *);
const char *CirName_cstr(CirName);
void CirName_log(CirName);
#define CirName_equals(a, b) ((a) == (b))

// Attributes
const CirAttr *CirAttr_int(int32_t);
// NOTE: string is not copied!
const CirAttr *CirAttr_str(const char *);
const CirAttr *CirAttr_name(CirName);
const CirAttr *CirAttr_cons(CirName, const CirAttr **, size_t len);
bool CirAttr_isInt(const CirAttr *);
bool CirAttr_isStr(const CirAttr *);
bool CirAttr_isName(const CirAttr *);
bool CirAttr_isCons(const CirAttr *);
CirName CirAttr_getName(const CirAttr *);
size_t CirAttr_getNumArgs(const CirAttr *);
const CirAttr * const *CirAttr_getArgs(const CirAttr *);
void CirAttr_log(const CirAttr *);

// Types
bool CirType_isVoid(const CirType *);
uint32_t CirType_isInt(const CirType *); // returns ikind
uint32_t CirType_isFloat(const CirType *); // returns fkind
bool CirType_isArithmetic(const CirType *); // true if is int or float
bool CirType_isPtr(const CirType *);
bool CirType_isArray(const CirType *);
bool CirType_isFun(const CirType *);
bool CirType_isNamed(const CirType *);
bool CirType_isComp(const CirType *);
bool CirType_isVaList(const CirType *);
const CirType *CirType_getBaseType(const CirType *);
CirTypedefId CirType_getTypedefId(const CirType *);
CirCompId CirType_getCompId(const CirType *);
const CirType *CirType_void(void);
const CirType *CirType_int(uint32_t ikind);
const CirType *CirType_float(uint32_t fkind);
const CirType *CirType_typedef(CirTypedefId);
const CirType *CirType_comp(CirCompId);
const CirType *CirType_ptr(const CirType *);
const CirType *CirType_array(const CirType *);
const CirType *CirType_arrayWithLen(const CirType *, uint32_t len);
const CirType *CirType_fun(const CirType *, const CirFunParam *params, size_t numParams, bool isVa);
const CirType *CirType_valist(void);
const CirType *CirType_unroll(const CirType *);
const CirType *CirType_unrollDeep(const CirType *);
size_t CirType_getNumAttrs(const CirType *);
const CirAttr * const *CirType_getAttrs(const CirType *);
// will replace existing attrs
const CirType *CirType_withAttrs(const CirType *, const CirAttr * const *attrs, size_t numAttrs);
const CirType *CirType_replaceAttrs(const CirType *, const CirAttr *const *attrs, size_t numAttrs);
size_t CirType_getNumParams(const CirType *);
const CirFunParam *CirType_getParams(const CirType *);
bool CirType_isParamsVa(const CirType *);
bool CirType_hasArrayLen(const CirType *);
uint32_t CirType_getArrayLen(const CirType *);
uint64_t CirType_alignof(const CirType *, const CirMachine *); // Returns alignment in bytes
uint64_t CirType_sizeof(const CirType *, const CirMachine *);
void CirType_log(const CirType *, const char *name);

// Var
CirVarId CirVar_new(CirCodeId ownerOrZero);
CirName CirVar_getName(CirVarId);
void CirVar_setName(CirVarId, CirName);
const CirType *CirVar_getType(CirVarId);
void CirVar_setType(CirVarId, const CirType *);
CirVarId CirVar_getFormal(CirVarId, size_t i);
void CirVar_setFormal(CirVarId, size_t, CirVarId);
CirCodeId CirVar_getCode(CirVarId);
CirCodeId CirVar_getOwner(CirVarId);
CirStorage CirVar_getStorage(CirVarId);
void CirVar_setStorage(CirVarId, CirStorage);
void CirVar_log(CirVarId);
void CirVar_logNameAndType(CirVarId);

// Typedef
CirTypedefId CirTypedef_new(CirName, const CirType *);
CirName CirTypedef_getName(CirTypedefId);
const CirType *CirTypedef_getType(CirTypedefId);
void CirTypedef_log(CirTypedefId);

// Comp
CirCompId CirComp_new(void);
bool CirComp_isStruct(CirCompId);
void CirComp_setStruct(CirCompId, bool);
bool CirComp_isDefined(CirCompId);
void CirComp_setDefined(CirCompId, bool);
CirName CirComp_getName(CirCompId);
void CirComp_setName(CirCompId, CirName);
size_t CirComp_getNumFields(CirCompId);
void CirComp_setNumFields(CirCompId, size_t);
CirName CirComp_getFieldName(CirCompId, size_t);
void CirComp_setFieldName(CirCompId, size_t, CirName);
const CirType *CirComp_getFieldType(CirCompId, size_t);
void CirComp_setFieldType(CirCompId, size_t, const CirType *);
bool CirComp_hasFieldBitsize(CirCompId, size_t);
size_t CirComp_getFieldBitsize(CirCompId, size_t);
void CirComp_setFieldBitsize(CirCompId, size_t, size_t);
void CirComp_clearFieldBitsize(CirCompId, size_t);
bool CirComp_getFieldByName(CirCompId, CirName, size_t *);
uint64_t CirComp_getFieldAlign(CirCompId, size_t, const CirMachine *);
uint64_t CirComp_getAlign(CirCompId, const CirMachine *);
uint64_t CirComp_getSize(CirCompId, const CirMachine *);
uint64_t CirComp_getFieldBitsOffset(CirCompId, size_t, const CirMachine *);
void CirComp_log(CirCompId cid);

// Value
const CirValue *CirValue_ofU64(uint32_t ikind, uint64_t val);
const CirValue *CirValue_ofI64(uint32_t ikind, int64_t val);
const CirValue *CirValue_ofVar(CirVarId vid);
const CirValue *CirValue_ofMem(CirVarId vid);
// NOTE: String is not copied!
const CirValue *CirValue_ofString(const char *, size_t len);
const CirValue *CirValue_ofCString(const char *);
uint32_t CirValue_isInt(const CirValue *);
bool CirValue_isString(const CirValue *);
bool CirValue_isVar(const CirValue *);
bool CirValue_isMem(const CirValue *);
bool CirValue_isLval(const CirValue *); // Var or Mem
uint64_t CirValue_getU64(const CirValue *);
int64_t CirValue_getI64(const CirValue *);
const char *CirValue_getString(const CirValue *);
size_t CirValue_getNumFields(const CirValue *);
CirName CirValue_getField(const CirValue *, size_t);
const CirValue *CirValue_withFields(const CirValue *, const CirName *fields, size_t len);
CirVarId CirValue_getVar(const CirValue *);
uint64_t CirValue_computeBitsOffset(const CirValue *, const CirMachine *);
const CirType *CirValue_getType(const CirValue *);
void CirValue_log(const CirValue *);

// Stmts
CirStmtId CirStmt_newAfter(CirStmtId sid); // Create a new empty NOP stmt after sid
void CirStmt_toNop(CirStmtId sid);
void CirStmt_toUnOp(CirStmtId sid, const CirValue *dst, uint32_t unop, const CirValue *op1);
void CirStmt_toBinOp(CirStmtId sid, const CirValue *dst, uint32_t binop, const CirValue *op1, const CirValue *op2);
void CirStmt_toCall(CirStmtId sid, const CirValue *dst, const CirValue *target, const CirValue *const *args, size_t numArgs);
void CirStmt_toReturn(CirStmtId sid, const CirValue *value);
void CirStmt_toCmp(CirStmtId sid, uint32_t condop, const CirValue *op1, const CirValue *op2, CirStmtId target);
void CirStmt_toGoto(CirStmtId sid, CirStmtId jumpTarget);
bool CirStmt_isNop(CirStmtId sid);
bool CirStmt_isUnOp(CirStmtId sid);
bool CirStmt_isBinOp(CirStmtId sid);
bool CirStmt_isCall(CirStmtId sid);
bool CirStmt_isReturn(CirStmtId sid);
bool CirStmt_isCmp(CirStmtId sid);
bool CirStmt_isGoto(CirStmtId sid);
bool CirStmt_isJump(CirStmtId sid);
uint32_t CirStmt_getOp(CirStmtId stmt_id);
const CirValue *CirStmt_getDst(CirStmtId);
const CirValue *CirStmt_getOperand1(CirStmtId);
const CirValue *CirStmt_getOperand2(CirStmtId);
size_t CirStmt_getNumArgs(CirStmtId);
const CirValue *CirStmt_getArg(CirStmtId, size_t);
CirStmtId CirStmt_getJumpTarget(CirStmtId);
void CirStmt_setJumpTarget(CirStmtId, CirStmtId);
CirStmtId CirStmt_getNext(CirStmtId sid);
CirStmtId CirStmt_getPrev(CirStmtId sid);
void CirStmt_log(CirStmtId sid);

// CirCode
CirCodeId CirCode_ofExpr(const CirValue *); // Create an empty expr block
CirCodeId CirCode_ofCond(void); // Create an empty cond block
CirStmtId CirCode_appendNewStmt(CirCodeId); // Append a new NOP stmt
bool CirCode_isExpr(CirCodeId);
bool CirCode_isCond(CirCodeId);
void CirCode_free(CirCodeId);
const CirValue *CirCode_getValue(CirCodeId);
void CirCode_setValue(CirCodeId, const CirValue *);
CirStmtId CirCode_getFirstStmt(CirCodeId);
CirStmtId CirCode_getLastStmt(CirCodeId);
void CirCode_addTrueJump(CirCodeId, CirStmtId);
void CirCode_addFalseJump(CirCodeId, CirStmtId);
CirCodeId CirCode_toExpr(CirCodeId, bool dropValue);
// Always returns a non-NULL type.
const CirType *CirCode_getType(CirCodeId);
void CirCode_append(CirCodeId a, CirCodeId b);
void CirCode_dump(CirCodeId cid);
size_t CirCode_getNumVars(CirCodeId cid);
CirVarId CirCode_getVar(CirCodeId cid, size_t i);

#endif // CIR_H
