#ifndef CIR_H
#define CIR_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

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

// ikind
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

// fkind
#define CIR_FFLOAT 13
#define CIR_FDOUBLE 14
#define CIR_FLONGDOUBLE 15
#define CIR_F128 16

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

// Builtins
enum {
    CIR_BUILTIN_BSWAP16 = 1,
    CIR_BUILTIN_BSWAP32,
    CIR_BUILTIN_BSWAP64,
    CIR_NUM_BUILTINS
};

typedef struct CirMachine CirMachine;
typedef uint32_t CirName; // Reference type
typedef struct CirType CirType; // Value type
typedef uint32_t CirCompId; // Reference type
typedef uint32_t CirEnumId;
typedef uint32_t CirEnumItemId;
typedef uint32_t CirTypedefId; // Reference type
typedef uint32_t CirVarId; // Reference type
typedef uint32_t CirStmtId; // Reference type
typedef struct CirValue CirValue;
typedef uint32_t CirCodeId;
typedef struct CirAttr CirAttr;
typedef uint32_t CirStorage;
typedef uint32_t CirBuiltinId;

typedef struct CirFunParam {
    CirName name;
    const CirType *type;
} CirFunParam;

// This allows us to easily extract the names of all symbols in CIL's API.
#define CIRAPI(x) x

void CIRAPI(CirLog_begin)(uint32_t level);
void CIRAPI(CirLog_print)(const char *);
void CIRAPI(CirLog_printb)(const void *, size_t);
void CIRAPI(CirLog_printf)(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void CIRAPI(CirLog_vprintf)(const char *fmt, va_list va);
void CIRAPI(CirLog_printq)(const char *);
void CIRAPI(CirLog_printqb)(const char *, size_t);
void CIRAPI(CirLog_end)(void);
void CIRAPI(cir_fatal)(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
void CIRAPI(cir_bug)(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
void CIRAPI(cir_warn)(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void CIRAPI(cir_log)(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

// TODO: Why is this a public API lol
uint64_t CIRAPI(CirPrime_ge)(uint64_t target);

void CirBreak(void);

// CirMem
void *CIRAPI(CirMem_balloc)(size_t n, size_t align) __attribute__((malloc));

// CirMachine
const CirMachine *CIRAPI(CirMachine_getBuild)(void);
const CirMachine *CIRAPI(CirMachine_getHost)(void);

// CirIkind
uint32_t CIRAPI(CirIkind_size)(uint32_t ikind, const CirMachine *);
bool CIRAPI(CirIkind_isSigned)(uint32_t ikind, const CirMachine *);
uint32_t CIRAPI(CirIkind_toUnsigned)(uint32_t ikind);
uint32_t CIRAPI(CirIkind_fromSize)(uint32_t size, bool _unsigned, const CirMachine *);

// CirFkind
uint32_t CIRAPI(CirFkind_size)(uint32_t fkind, const CirMachine *);
uint32_t CIRAPI(CirFkind_fromSize)(uint32_t size, const CirMachine *);

// CirName
CirName CIRAPI(CirName_of)(const char *);
const char *CIRAPI(CirName_cstr)(CirName);
void CIRAPI(CirName_log)(CirName);
#define CirName_equals(a, b) ((a) == (b))

// Attributes
const CirAttr *CIRAPI(CirAttr_int)(int32_t);
// NOTE: string is not copied!
const CirAttr *CIRAPI(CirAttr_str)(const char *);
const CirAttr *CIRAPI(CirAttr_name)(CirName);
const CirAttr *CIRAPI(CirAttr_cons)(CirName, const CirAttr **, size_t len);
bool CIRAPI(CirAttr_isInt)(const CirAttr *);
bool CIRAPI(CirAttr_isStr)(const CirAttr *);
bool CIRAPI(CirAttr_isName)(const CirAttr *);
bool CIRAPI(CirAttr_isCons)(const CirAttr *);
CirName CIRAPI(CirAttr_getName)(const CirAttr *);
size_t CIRAPI(CirAttr_getNumArgs)(const CirAttr *);
const CirAttr * const *CIRAPI(CirAttr_getArgs)(const CirAttr *);
void CIRAPI(CirAttr_log)(const CirAttr *);

// Types
bool CIRAPI(CirType_isVoid)(const CirType *);
uint32_t CIRAPI(CirType_isInt)(const CirType *); // returns ikind
uint32_t CIRAPI(CirType_isFloat)(const CirType *); // returns fkind
bool CIRAPI(CirType_isArithmetic)(const CirType *); // true if is int or float
bool CIRAPI(CirType_isPtr)(const CirType *);
bool CIRAPI(CirType_isArray)(const CirType *);
bool CIRAPI(CirType_isFun)(const CirType *);
bool CIRAPI(CirType_isNamed)(const CirType *);
bool CIRAPI(CirType_isComp)(const CirType *);
bool CIRAPI(CirType_isEnum)(const CirType *);
bool CIRAPI(CirType_isVaList)(const CirType *);
const CirType *CIRAPI(CirType_getBaseType)(const CirType *);
CirTypedefId CIRAPI(CirType_getTypedefId)(const CirType *);
CirCompId CIRAPI(CirType_getCompId)(const CirType *);
CirEnumId CIRAPI(CirType_getEnumId)(const CirType *);
const CirType *CIRAPI(CirType_void)(void);
const CirType *CIRAPI(CirType_int)(uint32_t ikind);
const CirType *CIRAPI(CirType_float)(uint32_t fkind);
const CirType *CIRAPI(CirType_typedef)(CirTypedefId);
const CirType *CIRAPI(CirType_comp)(CirCompId);
const CirType *CIRAPI(CirType_enum)(CirEnumId);
const CirType *CIRAPI(CirType_ptr)(const CirType *);
const CirType *CIRAPI(CirType_array)(const CirType *);
const CirType *CIRAPI(CirType_arrayWithLen)(const CirType *, uint32_t len);
const CirType *CIRAPI(CirType_fun)(const CirType *, const CirFunParam *params, size_t numParams, bool isVa);
const CirType *CIRAPI(CirType_valist)(void);
const CirType *CIRAPI(CirType_unroll)(const CirType *);
const CirType *CIRAPI(CirType_unrollDeep)(const CirType *);
const CirType *CIRAPI(CirType_removeQual)(const CirType *);
const CirType *CIRAPI(CirType_lvalConv)(const CirType *);
size_t CIRAPI(CirType_getNumAttrs)(const CirType *);
const CirAttr * const *CIRAPI(CirType_getAttrs)(const CirType *);
// will replace existing attrs
const CirType *CIRAPI(CirType_withAttrs)(const CirType *, const CirAttr * const *attrs, size_t numAttrs);
const CirType *CIRAPI(CirType_replaceAttrs)(const CirType *, const CirAttr *const *attrs, size_t numAttrs);
const CirType *CIRAPI(CirType_removeAttrs)(const CirType *, const CirAttr * const *attrs, size_t numAttrs);
size_t CIRAPI(CirType_getNumParams)(const CirType *);
const CirFunParam *CIRAPI(CirType_getParams)(const CirType *);
bool CIRAPI(CirType_isParamsVa)(const CirType *);
bool CIRAPI(CirType_hasArrayLen)(const CirType *);
uint32_t CIRAPI(CirType_getArrayLen)(const CirType *);
uint64_t CIRAPI(CirType_alignof)(const CirType *, const CirMachine *); // Returns alignment in bytes
uint64_t CIRAPI(CirType_sizeof)(const CirType *, const CirMachine *);
void CIRAPI(CirType_log)(const CirType *, const char *name);
bool CIRAPI(CirType_equals)(const CirType *, const CirType *);

// Builtin functions
CirBuiltinId CIRAPI(CirBuiltin_ofName)(CirName);
CirName CIRAPI(CirBuiltin_getName)(CirBuiltinId);
const CirType *CIRAPI(CirBuiltin_getType)(CirBuiltinId);

// Var
CirVarId CIRAPI(CirVar_new)(CirCodeId ownerOrZero);
CirName CIRAPI(CirVar_getName)(CirVarId);
void CIRAPI(CirVar_setName)(CirVarId, CirName);
const CirType *CIRAPI(CirVar_getType)(CirVarId);
void CIRAPI(CirVar_setType)(CirVarId, const CirType *);
CirVarId CIRAPI(CirVar_getFormal)(CirVarId, size_t i);
void CIRAPI(CirVar_setFormal)(CirVarId, size_t, CirVarId);
CirCodeId CIRAPI(CirVar_getCode)(CirVarId);
void CIRAPI(CirVar_setCode)(CirVarId, CirCodeId);
CirCodeId CIRAPI(CirVar_getOwner)(CirVarId);
CirStorage CIRAPI(CirVar_getStorage)(CirVarId);
void CIRAPI(CirVar_setStorage)(CirVarId, CirStorage);
void CIRAPI(CirVar_log)(CirVarId);
void CIRAPI(CirVar_logNameAndType)(CirVarId);

// Typedef
CirTypedefId CIRAPI(CirTypedef_new)(CirName, const CirType *);
CirName CIRAPI(CirTypedef_getName)(CirTypedefId);
const CirType *CIRAPI(CirTypedef_getType)(CirTypedefId);
void CIRAPI(CirTypedef_log)(CirTypedefId);

// Comp
CirCompId CIRAPI(CirComp_new)(void);
bool CIRAPI(CirComp_isStruct)(CirCompId);
void CIRAPI(CirComp_setStruct)(CirCompId, bool);
bool CIRAPI(CirComp_isDefined)(CirCompId);
void CIRAPI(CirComp_setDefined)(CirCompId, bool);
CirName CIRAPI(CirComp_getName)(CirCompId);
void CIRAPI(CirComp_setName)(CirCompId, CirName);
size_t CIRAPI(CirComp_getNumFields)(CirCompId);
void CIRAPI(CirComp_setNumFields)(CirCompId, size_t);
CirName CIRAPI(CirComp_getFieldName)(CirCompId, size_t);
void CIRAPI(CirComp_setFieldName)(CirCompId, size_t, CirName);
const CirType *CIRAPI(CirComp_getFieldType)(CirCompId, size_t);
void CIRAPI(CirComp_setFieldType)(CirCompId, size_t, const CirType *);
bool CIRAPI(CirComp_hasFieldBitsize)(CirCompId, size_t);
size_t CIRAPI(CirComp_getFieldBitsize)(CirCompId, size_t);
void CIRAPI(CirComp_setFieldBitsize)(CirCompId, size_t, size_t);
void CIRAPI(CirComp_clearFieldBitsize)(CirCompId, size_t);
bool CIRAPI(CirComp_getFieldByName)(CirCompId, CirName, size_t *);
uint64_t CIRAPI(CirComp_getFieldAlign)(CirCompId, size_t, const CirMachine *);
uint64_t CIRAPI(CirComp_getAlign)(CirCompId, const CirMachine *);
uint64_t CIRAPI(CirComp_getSize)(CirCompId, const CirMachine *);
uint64_t CIRAPI(CirComp_getFieldBitsOffset)(CirCompId, size_t, const CirMachine *);
void CIRAPI(CirComp_log)(CirCompId cid);

// Enum
CirEnumId CIRAPI(CirEnum_new)(void);
CirName CIRAPI(CirEnum_getName)(CirEnumId);
void CIRAPI(CirEnum_setName)(CirEnumId, CirName);
size_t CIRAPI(CirEnum_getNumItems)(CirEnumId);
void CIRAPI(CirEnum_setNumItems)(CirEnumId, size_t);
CirEnumItemId CIRAPI(CirEnum_getItem)(CirEnumId, size_t);
void CIRAPI(CirEnum_setItem)(CirEnumId, size_t, CirEnumItemId);
// The integer kind used to represent this enum.
// Per ANSI-C, this should always be CIR_IINT, but gcc allows other integer kinds.
uint32_t CIRAPI(CirEnum_getIkind)(CirEnumId);
void CIRAPI(CirEnum_setIkind)(CirEnumId, uint32_t);
bool CIRAPI(CirEnum_isDefined)(CirEnumId);
void CIRAPI(CirEnum_setDefined)(CirEnumId, bool);

// EnumItem
CirEnumItemId CIRAPI(CirEnumItem_new)(CirName name);
CirName CIRAPI(CirEnumItem_getName)(CirEnumItemId);
int64_t CIRAPI(CirEnumItem_getI64)(CirEnumItemId);
void CIRAPI(CirEnumItem_setI64)(CirEnumItemId, int64_t);

// Value
unsigned CIRAPI(CirValue_registerUser)(void);
const CirValue *CIRAPI(CirValue_ofU64)(uint32_t ikind, uint64_t val);
const CirValue *CIRAPI(CirValue_ofI64)(uint32_t ikind, int64_t val);
const CirValue *CIRAPI(CirValue_ofVar)(CirVarId vid);
const CirValue *CIRAPI(CirValue_ofMem)(CirVarId vid);
const CirValue *CIRAPI(CirValue_ofUser)(unsigned id, void *);
// NOTE: String is not copied!
const CirValue *CIRAPI(CirValue_ofString)(const char *, size_t len);
const CirValue *CIRAPI(CirValue_ofCString)(const char *);
const CirValue *CIRAPI(CirValue_ofType)(const CirType *);
const CirValue *CIRAPI(CirValue_ofBuiltin)(CirBuiltinId);
bool CIRAPI(CirValue_isInt)(const CirValue *);
bool CIRAPI(CirValue_isString)(const CirValue *);
bool CIRAPI(CirValue_isVar)(const CirValue *);
bool CIRAPI(CirValue_isMem)(const CirValue *);
bool CIRAPI(CirValue_isLval)(const CirValue *); // Var or Mem
unsigned CIRAPI(CirValue_isUser)(const CirValue *);
bool CIRAPI(CirValue_isType)(const CirValue *);
CirBuiltinId CIRAPI(CirValue_isBuiltin)(const CirValue *);
uint64_t CIRAPI(CirValue_getU64)(const CirValue *);
int64_t CIRAPI(CirValue_getI64)(const CirValue *);
const char *CIRAPI(CirValue_getString)(const CirValue *);
size_t CIRAPI(CirValue_getNumFields)(const CirValue *);
CirName CIRAPI(CirValue_getField)(const CirValue *, size_t);
const CirValue *CIRAPI(CirValue_withFields)(const CirValue *, const CirName *fields, size_t len);
CirVarId CIRAPI(CirValue_getVar)(const CirValue *);
const CirValue *CIRAPI(CirValue_withVar)(const CirValue *, CirVarId);
void *CIRAPI(CirValue_getPtr)(const CirValue *);
const CirType *CIRAPI(CirValue_getType)(const CirValue *); // Returns the (may be casted) type
const CirType *CIRAPI(CirValue_getRawType)(const CirValue *); // Returns the raw underlying type. Note that ints always have a cast type.
const CirType *CIRAPI(CirValue_getCastType)(const CirValue *); // Returns the cast type, if any
const CirValue *CIRAPI(CirValue_withCastType)(const CirValue *, const CirType *);
const CirType *CIRAPI(CirValue_getTypeValue)(const CirValue *);
void CIRAPI(CirValue_log)(const CirValue *);

// Stmts
unsigned CIRAPI(CirStmt_registerUser)(void);
CirStmtId CIRAPI(CirStmt_newOrphan)(void); // Create a new empty NOP stmt that is not owned by any code
CirStmtId CIRAPI(CirStmt_newAfter)(CirStmtId sid); // Create a new empty NOP stmt after sid
CirStmtId CIRAPI(CirStmt_newBefore)(CirStmtId); // Create a new empty NOP stmt after sid
void CIRAPI(CirStmt_orphanize)(CirStmtId); // Detach stmt from whatever code it is attached to, and make it an orphan
bool CIRAPI(CirStmt_isOrphan)(CirStmtId stmtId);
void CIRAPI(CirStmt_toNop)(CirStmtId sid);
void CIRAPI(CirStmt_toUnOp)(CirStmtId sid, const CirValue *dst, uint32_t unop, const CirValue *op1);
void CIRAPI(CirStmt_toBinOp)(CirStmtId sid, const CirValue *dst, uint32_t binop, const CirValue *op1, const CirValue *op2);
void CIRAPI(CirStmt_toCall)(CirStmtId sid, const CirValue *dst, const CirValue *target, const CirValue *const *args, size_t numArgs);
void CIRAPI(CirStmt_toReturn)(CirStmtId sid, const CirValue *value);
void CIRAPI(CirStmt_toCmp)(CirStmtId sid, uint32_t condop, const CirValue *op1, const CirValue *op2, CirStmtId target);
void CIRAPI(CirStmt_toGoto)(CirStmtId sid, CirStmtId jumpTarget);
void CIRAPI(CirStmt_toLabel)(CirStmtId sid, CirName labelName);
void CIRAPI(CirStmt_toGotoLabel)(CirStmtId sid, CirName labelName);
void CIRAPI(CirStmt_toUser)(CirStmtId, unsigned uid, void *ptr);
bool CIRAPI(CirStmt_isNop)(CirStmtId sid);
bool CIRAPI(CirStmt_isUnOp)(CirStmtId sid);
bool CIRAPI(CirStmt_isBinOp)(CirStmtId sid);
bool CIRAPI(CirStmt_isCall)(CirStmtId sid);
bool CIRAPI(CirStmt_isReturn)(CirStmtId sid);
bool CIRAPI(CirStmt_isCmp)(CirStmtId sid);
bool CIRAPI(CirStmt_isGoto)(CirStmtId sid);
bool CIRAPI(CirStmt_isJump)(CirStmtId sid);
bool CIRAPI(CirStmt_isLabel)(CirStmtId sid);
bool CIRAPI(CirStmt_isGotoLabel)(CirStmtId sid);
unsigned CIRAPI(CirStmt_isUser)(CirStmtId);
uint32_t CIRAPI(CirStmt_getOp)(CirStmtId stmt_id);
const CirValue *CIRAPI(CirStmt_getDst)(CirStmtId);
const CirValue *CIRAPI(CirStmt_getOperand1)(CirStmtId);
const CirValue *CIRAPI(CirStmt_getOperand2)(CirStmtId);
size_t CIRAPI(CirStmt_getNumOperands)(CirStmtId);
const CirValue *CIRAPI(CirStmt_getOperand)(CirStmtId, size_t i);
void CIRAPI(CirStmt_setOperand)(CirStmtId, size_t i, const CirValue *);
size_t CIRAPI(CirStmt_getNumArgs)(CirStmtId);
const CirValue *CIRAPI(CirStmt_getArg)(CirStmtId, size_t);
CirStmtId CIRAPI(CirStmt_getJumpTarget)(CirStmtId);
void CIRAPI(CirStmt_setJumpTarget)(CirStmtId, CirStmtId);
CirName CIRAPI(CirStmt_getLabelName)(CirStmtId);
void CIRAPI(CirStmt_setLabelName)(CirStmtId, CirName);
void *CIRAPI(CirStmt_getPtr)(CirStmtId);
void CIRAPI(CirStmt_setPtr)(CirStmtId, void *);
CirStmtId CIRAPI(CirStmt_getNext)(CirStmtId sid);
CirStmtId CIRAPI(CirStmt_getPrev)(CirStmtId sid);
void CIRAPI(CirStmt_log)(CirStmtId sid);
void CIRAPI(CirStmt_typecheck)(CirStmtId, const CirMachine *);

// CirCode
CirCodeId CIRAPI(CirCode_ofExpr)(const CirValue *); // Create an empty expr block
CirCodeId CIRAPI(CirCode_ofCond)(void); // Create an empty cond block
CirStmtId CIRAPI(CirCode_appendNewStmt)(CirCodeId); // Append a new NOP stmt
CirStmtId CIRAPI(CirCode_prependNewStmt)(CirCodeId); // Prepend a new NOP stmt
void CIRAPI(CirCode_appendOrphanStmt)(CirCodeId codeId, CirStmtId stmtId);
bool CIRAPI(CirCode_isExpr)(CirCodeId);
bool CIRAPI(CirCode_isCond)(CirCodeId);
void CIRAPI(CirCode_free)(CirCodeId);
const CirValue *CIRAPI(CirCode_getValue)(CirCodeId);
void CIRAPI(CirCode_setValue)(CirCodeId, const CirValue *);
CirStmtId CIRAPI(CirCode_getFirstStmt)(CirCodeId);
CirStmtId CIRAPI(CirCode_getLastStmt)(CirCodeId);
void CIRAPI(CirCode_addTrueJump)(CirCodeId, CirStmtId);
void CIRAPI(CirCode_addFalseJump)(CirCodeId, CirStmtId);
CirCodeId CIRAPI(CirCode_toExpr)(CirCodeId, bool dropValue);
// Always returns a non-NULL type.
const CirType *CIRAPI(CirCode_getType)(CirCodeId);
void CIRAPI(CirCode_append)(CirCodeId a, CirCodeId b);
void CIRAPI(CirCode_dump)(CirCodeId cid);
size_t CIRAPI(CirCode_getNumVars)(CirCodeId cid);
CirVarId CIRAPI(CirCode_getVar)(CirCodeId cid, size_t i);
void CIRAPI(CirCode_typecheck)(CirCodeId, const CirMachine *);
void CIRAPI(CirCode_resolveLabels)(CirCodeId);

#undef CIRAPI

#endif // CIR_H
