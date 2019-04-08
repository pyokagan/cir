#include "cir_internal.h"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>

// data1:
// bits 31 to 28: type
// bits 27 to 21: ikind/fkind/num params (7 bits) [u1]
// bits 20: isva/array len (1 bit) [u2]
// bits 19 to 15: num attrs (5 bits)

// type (4 bits)
#define CIR_TVOID 0
#define CIR_TINT 1
#define CIR_TFLOAT 2
#define CIR_TPTR 3
#define CIR_TARRAY 4
#define CIR_TFUN 5
#define CIR_TNAMED 6
#define CIR_TCOMP 7
#define CIR_TENUM 8
#define CIR_TVALIST 9

#define data1ToType(x) (((x) >> 28) & 0x0f)
#define typeToData1(x) (((x) & 0x0f) << 28)
#define data1ToU1(x) (((x) >> 21) & 0x7f)
#define u1ToData1(x) (((x) & 0x7f) << 21)
#define data1ToU2(x) (((x) >> 20) & 0x01)
#define u2ToData1(x) (((x) & 0x01) << 20)
#define data1ToNumAttrs(x) (((x) >> 15) & 0x1f)
#define numAttrsToData1(x) (((x) & 0x1f) << 15)
#define MAX_ATTRS 0x1f
#define MAX_FUN_PARAMS 0x7f

typedef struct CirType {
    uint32_t data1;
    const CirAttr *attrs[];
} CirType;

typedef struct CirTypePtr {
    uint32_t data1;
    const struct CirType *baseType;
    const CirAttr *attrs[];
} CirTypePtr;

typedef struct CirTypeArray {
    uint32_t data1;
    uint32_t arrayLen;
    const struct CirType *baseType;
    const CirAttr *attrs[];
} CirTypeArray;

typedef struct CirTypeNamed {
    uint32_t data1;
    CirTypedefId typedefId;
    const CirAttr *attrs[];
} CirTypeNamed;

typedef struct CirTypeComp {
    uint32_t data1;
    CirCompId compId;
    const CirAttr *attrs[];
} CirTypeComp;

typedef struct CirTypeEnum {
    uint32_t data1;
    CirEnumId enumId;
    const CirAttr *attrs[];
} CirTypeEnum;

typedef struct CirTypeFun {
    uint32_t data1;
    const struct CirType *baseType;
    CirFunParam funParams[];
} CirTypeFun;

_Static_assert(alignof(CirFunParam) >= alignof(const CirAttr *), "alignment");

static const CirType voidType = {
    .data1 = typeToData1(CIR_TVOID),
};
static const CirType shortType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_ISHORT),
};
static const CirType ushortType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IUSHORT),
};
static const CirType intType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IINT),
};
static const CirType uintType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IUINT),
};
static const CirType longType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_ILONG),
};
static const CirType ulongType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IULONG),
};
static const CirType charType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_ICHAR),
};
static const CirType scharType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_ISCHAR),
};
static const CirType ucharType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IUCHAR),
};
static const CirType boolType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IBOOL),
};
static const CirType longlongType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_ILONGLONG),
};
static const CirType ulonglongType = {
    .data1 = typeToData1(CIR_TINT) | u1ToData1(CIR_IULONGLONG),
};
static const CirType floatType = {
    .data1 = typeToData1(CIR_TFLOAT) | u1ToData1(CIR_FFLOAT),
};
static const CirType doubleType = {
    .data1 = typeToData1(CIR_TFLOAT) | u1ToData1(CIR_FDOUBLE),
};
static const CirType longdoubleType = {
    .data1 = typeToData1(CIR_TFLOAT) | u1ToData1(CIR_FLONGDOUBLE),
};
static const CirType f128Type = {
    .data1 = typeToData1(CIR_TFLOAT) | u1ToData1(CIR_F128),
};
static const CirType valistType = {
    .data1 = typeToData1(CIR_TVALIST),
};

bool
CirType_isVoid(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TVOID;
}

uint32_t
CirType_isInt(const CirType *type)
{
    assert(type);
    if (data1ToType(type->data1) != CIR_TINT)
        return 0;
    return data1ToU1(type->data1);
}

uint32_t
CirType_isFloat(const CirType *type)
{
    assert(type);
    if (data1ToType(type->data1) != CIR_TFLOAT)
        return 0;

    return data1ToU1(type->data1);
}

bool
CirType_isArithmetic(const CirType *type)
{
    return CirType_isInt(type) || CirType_isFloat(type);
}

bool
CirType_isPtr(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TPTR;
}

bool
CirType_isArray(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TARRAY;
}

bool
CirType_isFun(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TFUN;
}

bool
CirType_isNamed(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TNAMED;
}

bool
CirType_isComp(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TCOMP;
}

bool
CirType_isEnum(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TENUM;
}

bool
CirType_isVaList(const CirType *type)
{
    assert(type);
    return data1ToType(type->data1) == CIR_TVALIST;
}

const CirType *
CirType_getBaseType(const CirType *type)
{
    assert(type);
    switch (data1ToType(type->data1)) {
    case CIR_TPTR:
        return ((const CirTypePtr *) type)->baseType;
    case CIR_TARRAY:
        return ((const CirTypeArray *)type)->baseType;
    case CIR_TFUN:
        return ((const CirTypeFun *)type)->baseType;
    default:
        cir_fatal("CirType_getBaseType called on leaf type");
    }
}

CirTypedefId
CirType_getTypedefId(const CirType *type)
{
    assert(type);
    if (data1ToType(type->data1) != CIR_TNAMED)
        cir_fatal("CirType_getTypedefId called on non-named type");

    return ((const CirTypeNamed *)type)->typedefId;
}

CirCompId
CirType_getCompId(const CirType *type)
{
    assert(type);
    if (data1ToType(type->data1) != CIR_TCOMP)
        cir_fatal("CirType_getCompId called on non-comp type");

    return ((const CirTypeComp *)type)->compId;
}

CirEnumId
CirType_getEnumId(const CirType *type)
{
    assert(type);
    if (data1ToType(type->data1) != CIR_TENUM)
        cir_fatal("CirType_getEnumId called on non-enum type");

    return ((const CirTypeEnum *)type)->enumId;
}

static void
copyAttrs(const CirAttr ** restrict dst, const CirAttr * const * restrict src, uint32_t numAttrs)
{
    for (uint32_t i = 0; i < numAttrs; i++) {
        if (i > 0 && CirAttr_getName(src[i-1]) >= CirAttr_getName(src[i]))
            cir_bug("copyAttrs: src attrs is not sorted and unique");
        dst[i] = src[i];
    }
}

const CirType *
CirType__void(const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (!numAttrs)
        return &voidType;
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    CirType *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TVOID) | numAttrsToData1(numAttrs);
    copyAttrs(out->attrs, attrs, numAttrs);
    return out;
}

const CirType *
CirType_void(void)
{
    return CirType__void(NULL, 0);
}

const CirType *
CirType__int(uint32_t ikind, const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    if (numAttrs) {
        CirType *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
        out->data1 = typeToData1(CIR_TINT) | u1ToData1(ikind) | numAttrsToData1(numAttrs);
        copyAttrs(out->attrs, attrs, numAttrs);
        return out;
    }

    switch (ikind) {
    case CIR_ICHAR:
        return &charType;
    case CIR_ISCHAR:
        return &scharType;
    case CIR_IUCHAR:
        return &ucharType;
    case CIR_IBOOL:
        return &boolType;
    case CIR_IINT:
        return &intType;
    case CIR_IUINT:
        return &uintType;
    case CIR_ISHORT:
        return &shortType;
    case CIR_IUSHORT:
        return &ushortType;
    case CIR_ILONG:
        return &longType;
    case CIR_IULONG:
        return &ulongType;
    case CIR_ILONGLONG:
        return &longlongType;
    case CIR_IULONGLONG:
        return &ulonglongType;
    default:
        cir_bug("unknown ikind");
    }
}

const CirType *
CirType_int(uint32_t ikind)
{
    return CirType__int(ikind, NULL, 0);
}

const CirType *
CirType__float(uint32_t fkind, const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    if (numAttrs) {
        CirType *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
        out->data1 = typeToData1(CIR_TFLOAT) | u1ToData1(fkind) | numAttrsToData1(numAttrs);
        copyAttrs(out->attrs, attrs, numAttrs);
        return out;
    }

    switch (fkind) {
    case CIR_FFLOAT:
        return &floatType;
    case CIR_FDOUBLE:
        return &doubleType;
    case CIR_FLONGDOUBLE:
        return &longdoubleType;
    case CIR_F128:
        return &f128Type;
    default:
        cir_bug("invalid fkind");
    }
}

const CirType *
CirType_float(uint32_t fkind)
{
    return CirType__float(fkind, NULL, 0);
}

const CirType *
CirType__typedef(CirTypedefId tid, const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    CirTypeNamed *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TNAMED) | numAttrsToData1(numAttrs);
    out->typedefId = tid;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_typedef(CirTypedefId tid)
{
    return CirType__typedef(tid, NULL, 0);
}

const CirType *
CirType__comp(CirCompId cid, const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    CirTypeComp *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TCOMP) | numAttrsToData1(numAttrs);
    out->compId = cid;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_comp(CirCompId cid)
{
    return CirType__comp(cid, NULL, 0);
}

const CirType *
CirType__enum(CirEnumId enumId, const CirAttr * const *attrs, uint32_t numAttrs)
{
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    CirTypeEnum *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TENUM) | numAttrsToData1(numAttrs);
    out->enumId = enumId;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_enum(CirEnumId enumId)
{
    return CirType__enum(enumId, NULL, 0);
}

const CirType *
CirType__ptr(const CirType *bt, const CirAttr * const *attrs, size_t numAttrs)
{
    assert(bt != NULL);
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");
    CirTypePtr *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TPTR) | numAttrsToData1(numAttrs);
    out->baseType = bt;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_ptr(const CirType *bt)
{
    return CirType__ptr(bt, NULL, 0);
}

const CirType *
CirType__array(const CirType *bt, const CirAttr * const *attrs, uint32_t numAttrs)
{
    assert(bt != NULL);
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");
    CirTypeArray *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TARRAY) | numAttrsToData1(numAttrs);
    out->arrayLen = 0;
    out->baseType = bt;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_array(const CirType *bt)
{
    return CirType__array(bt, NULL, 0);
}

const CirType *
CirType__arrayWithLen(const CirType *bt, uint32_t len, const CirAttr * const *attrs, uint32_t numAttrs)
{
    assert(bt != NULL);
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");
    CirTypeArray *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TARRAY) | u2ToData1(1) | numAttrsToData1(numAttrs);
    out->arrayLen = len;
    out->baseType = bt;
    copyAttrs(out->attrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_arrayWithLen(const CirType *bt, uint32_t len)
{
    return CirType__arrayWithLen(bt, len, NULL, 0);
}

const CirType *
CirType__fun(const CirType *bt, const CirFunParam *params, size_t numParams, bool isVa, const CirAttr * const *attrs, size_t numAttrs)
{
    assert(bt != NULL);
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");
    if (numParams > MAX_FUN_PARAMS)
        cir_bug("too many params");
    CirTypeFun *out = CirMem_balloc(sizeof(*out) + sizeof(*params) * numParams + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TFUN) | u1ToData1(numParams) | u2ToData1(isVa) | numAttrsToData1(numAttrs);
    out->baseType = bt;
    // Copy params
    for (size_t i = 0; i < numParams; i++)
        out->funParams[i] = params[i];
    const CirFunParam *lastParam = out->funParams + numParams;
    const CirAttr **outAttrs = (const CirAttr **)lastParam;
    copyAttrs(outAttrs, attrs, numAttrs);
    return (CirType *)out;
}

const CirType *
CirType_fun(const CirType *bt, const CirFunParam *params, size_t numParams, bool isVa)
{
    return CirType__fun(bt, params, numParams, isVa, NULL, 0);
}

const CirType *
CirType__valist(const CirAttr * const *attrs, size_t numAttrs)
{
    if (!numAttrs)
        return &valistType;
    if (numAttrs > MAX_ATTRS)
        cir_bug("too many attrs");

    CirType *out = CirMem_balloc(sizeof(*out) + sizeof(*attrs) * numAttrs, alignof(*out));
    out->data1 = typeToData1(CIR_TVALIST) | numAttrsToData1(numAttrs);
    copyAttrs(out->attrs, attrs, numAttrs);
    return out;
}

const CirType *
CirType_valist(void)
{
    return CirType__valist(NULL, 0);
}

// Returns array decayed into pointer
const CirType *
CirType__arrayToPtr(const CirType *tt)
{
    if (!CirType_isArray(tt))
        return tt;
    return CirType__ptr(CirType_getBaseType(tt), CirType_getAttrs(tt), CirType_getNumAttrs(tt));
}

const CirType *
CirType_unroll(const CirType *t)
{
    for (;;) {
        if (CirType_isNamed(t)) {
            CirTypedefId tid = CirType_getTypedefId(t);
            const CirType *bt = CirTypedef_getType(tid);
            const CirAttr *const *attrs = CirType_getAttrs(t);
            size_t numAttrs = CirType_getNumAttrs(t);
            t = CirType_withAttrs(bt, attrs, numAttrs);
        } else {
            return t;
        }
    }
}

const CirType *
CirType_unrollDeep(const CirType *t)
{
    const CirAttr *const *attrs = CirType_getAttrs(t);
    size_t numAttrs = CirType_getNumAttrs(t);

    switch (data1ToType(t->data1)) {
    case CIR_TVOID:
    case CIR_TINT:
    case CIR_TFLOAT:
    case CIR_TVALIST:
    case CIR_TCOMP:
    case CIR_TENUM:
        // Leaf
        return t;
    case CIR_TNAMED: {
        CirTypedefId tid = CirType_getTypedefId(t);
        const CirType *bt = CirTypedef_getType(tid);
        t = CirType_withAttrs(bt, attrs, numAttrs);
        return CirType_unrollDeep(t);
    }
    case CIR_TPTR: {
        const CirTypePtr *ptrType = (const CirTypePtr *)t;
        const CirType *bt = ptrType->baseType;
        bt = CirType_unrollDeep(bt);
        return CirType__ptr(bt, attrs, numAttrs);
    }
    case CIR_TARRAY: {
        const CirTypeArray *arrayType = (const CirTypeArray *)t;
        const CirType *bt = CirType_unrollDeep(arrayType->baseType);
        if (data1ToU2(t->data1))
            return CirType__arrayWithLen(bt, arrayType->arrayLen, attrs, numAttrs);
        else
            return CirType__array(bt, attrs, numAttrs);
    }
    case CIR_TFUN: {
        const CirTypeFun *funType = (const CirTypeFun *)t;
        const CirType *bt = CirType_unrollDeep(funType->baseType);
        size_t numParams = CirType_getNumParams(t);
        const CirFunParam *params = CirType_getParams(t);
        return CirType__fun(bt, params, numParams, data1ToU2(t->data1), attrs, numAttrs);
    }
    default:
        cir_bug("CirType_unrollDeep: unhandled type case");
    }
}

const CirType *
CirType_removeQual(const CirType *type)
{
    // TODO: optimize
    const CirAttr *attrs[] = {
        CirAttr_name(CirName_of("const")),
        CirAttr_name(CirName_of("restrict")),
        CirAttr_name(CirName_of("volatile"))
    };
    return CirType_removeAttrs(type, attrs, 3);
}

const CirType *
CirType_lvalConv(const CirType *type)
{
    const CirType *unrolledType = CirType_unroll(type);
    if (CirType_isFun(unrolledType)) {
        return CirType_ptr(type);
    } else if (CirType_isArray(unrolledType)) {
        return CirType__ptr(CirType_getBaseType(unrolledType), CirType_getAttrs(unrolledType), CirType_getNumAttrs(unrolledType));
    } else {
        const CirType *unqualType = CirType_removeQual(unrolledType);
        return unqualType == unrolledType ? type : unqualType;
    }
}

size_t
CirType_getNumAttrs(const CirType *t)
{
    return data1ToNumAttrs(t->data1);
}

const CirAttr * const *
CirType_getAttrs(const CirType *t)
{
    switch (data1ToType(t->data1)) {
    case CIR_TVOID:
    case CIR_TINT:
    case CIR_TFLOAT:
    case CIR_TVALIST:
        return t->attrs;
    case CIR_TPTR:
        return ((const CirTypePtr *)t)->attrs;
    case CIR_TARRAY:
        return ((const CirTypeArray *)t)->attrs;
    case CIR_TFUN: {
        size_t numParams = CirType_getNumParams(t);
        const CirFunParam *lastParam = ((const CirTypeFun *)t)->funParams + numParams;
        return (const CirAttr * const *)lastParam;
    }
    case CIR_TNAMED:
        return ((const CirTypeNamed *)t)->attrs;
    case CIR_TCOMP:
        return ((const CirTypeComp *)t)->attrs;
    case CIR_TENUM:
        return ((const CirTypeEnum *)t)->attrs;
    default:
        cir_bug("unknown type???");
    }
}

const CirType *
CirType_withAttrs(const CirType *t, const CirAttr * const *attrs, size_t numAttrs)
{
    if (!numAttrs)
        return t; // No-op

    CirAttrArray arr = CIRARRAY_INIT;
    CirAttrArray__merge(&arr, attrs, numAttrs, CirType_getAttrs(t), CirType_getNumAttrs(t));
    if (arr.len > MAX_ATTRS)
        cir_bug("too many attrs after merging");

    const CirType *ret = CirType_replaceAttrs(t, arr.items, arr.len);

    CirArray_release(&arr);
    return ret;
}

const CirType *
CirType_replaceAttrs(const CirType *t, const CirAttr *const *attrs, size_t numAttrs)
{
    // Simple no-op optimization
    size_t originalNumAttrs = CirType_getNumAttrs(t);
    if (!originalNumAttrs && !numAttrs)
        return t;

    const CirType *bt;
    const CirTypeNamed *tmpNamed;
    const CirTypeComp *tmpComp;
    const CirTypeArray *tmpArray;
    const CirTypeEnum *tmpEnum;
    switch (data1ToType(t->data1)) {
    case CIR_TVOID:
        return CirType__void(attrs, numAttrs);
    case CIR_TINT:
        return CirType__int(data1ToU1(t->data1), attrs, numAttrs);
    case CIR_TFLOAT:
        return CirType__float(data1ToU1(t->data1), attrs, numAttrs);
    case CIR_TNAMED:
        tmpNamed = (const CirTypeNamed *)t;
        return CirType__typedef(tmpNamed->typedefId, attrs, numAttrs);
    case CIR_TCOMP:
        tmpComp = (const CirTypeComp *)t;
        return CirType__comp(tmpComp->compId, attrs, numAttrs);
    case CIR_TENUM:
        tmpEnum = (const CirTypeEnum *)t;
        return CirType__enum(tmpEnum->enumId, attrs, numAttrs);
    case CIR_TPTR:
        bt = CirType_getBaseType(t);
        return CirType__ptr(bt, attrs, numAttrs);
    case CIR_TARRAY:
        tmpArray = (const CirTypeArray *)t;
        bt = tmpArray->baseType;
        if (data1ToU2(t->data1))
            return CirType__arrayWithLen(bt, tmpArray->arrayLen, attrs, numAttrs);
        else
            return CirType__array(bt, attrs, numAttrs);
    case CIR_TFUN:
        cir_bug("TODO: CIR_TFUN");
    case CIR_TVALIST:
        return CirType__valist(attrs, numAttrs);
    default:
        cir_bug("unknown type???");
    }
}

const CirType *
CirType_removeAttrs(const CirType *t, const CirAttr *const *attrs, size_t numAttrs)
{
    if (!numAttrs)
        return t; // No-op

    size_t initialNumAttrs = CirType_getNumAttrs(t);
    CirAttrArray arr = CIRARRAY_INIT;
    CirAttrArray__remove(&arr, CirType_getAttrs(t), initialNumAttrs, attrs, numAttrs);

    // Did any attrs actually get removed?
    if (arr.len == initialNumAttrs) {
        CirArray_release(&arr);
        return t;
    }

    const CirType *ret = CirType_replaceAttrs(t, arr.items, arr.len);
    CirArray_release(&arr);
    return ret;
}

size_t
CirType_getNumParams(const CirType *tt)
{
    if (!CirType_isFun(tt))
        cir_bug("CirType_getNumParams: not a function type");

    const CirTypeFun *t = (const CirTypeFun *)tt;
    return data1ToU1(t->data1);
}

const CirFunParam *
CirType_getParams(const CirType *tt)
{
    if (!CirType_isFun(tt))
        cir_bug("CirType_getParams: not a function type");

    return ((const CirTypeFun *)tt)->funParams;
}

bool
CirType_isParamsVa(const CirType *t)
{
    if (!CirType_isFun(t))
        cir_bug("CirType_isParamsVa: not a function type");

    return data1ToU2(t->data1);
}

bool
CirType_hasArrayLen(const CirType *t)
{
    if (!CirType_isArray(t))
        cir_bug("CirType_hasArrayLen: not an array type");

    return data1ToU2(t->data1);
}

uint32_t
CirType_getArrayLen(const CirType *t)
{
    if (!CirType_isArray(t))
        cir_bug("CirType_getArrayLen: not an array type");
    if (!CirType_hasArrayLen(t))
        cir_bug("CirType_getArrayLen: array type has no len");
    return ((const CirTypeArray *)t)->arrayLen;
}

const CirType *
CirType__integralPromotion(const CirType *t, const CirMachine *mach)
{
    const CirType *tu = CirType_unroll(t);
    const CirAttr *const *attrs = CirType_getAttrs(tu);
    size_t numAttrs = CirType_getNumAttrs(tu);
    uint32_t ikind = CirType_isInt(tu);
    if (ikind == CIR_IBOOL) {
        // _Bool can only be 0 or 1, irrespective of its size
        return CirType__int(CIR_IINT, attrs, numAttrs);
    } else if (ikind == CIR_ISHORT || ikind == CIR_IUSHORT || ikind == CIR_ICHAR || ikind == CIR_ISCHAR || ikind == CIR_IUCHAR) {
        if (CirIkind_size(ikind, mach) < CirIkind_size(CIR_IINT, mach) || CirIkind_isSigned(ikind, mach)) {
            return CirType__int(CIR_IINT, attrs, numAttrs);
        } else {
            return CirType__int(CIR_IUINT, attrs, numAttrs);
        }
    } else if (ikind) {
        return t;
    } else {
        cir_bug("CirType__integralPromotion: not expecting this type");
    }
}

static unsigned
intRank(uint32_t ikind)
{
    switch (ikind) {
    case CIR_IBOOL:
        return 0;
    case CIR_ICHAR:
    case CIR_ISCHAR:
    case CIR_IUCHAR:
        return 1;
    case CIR_ISHORT:
    case CIR_IUSHORT:
        return 2;
    case CIR_IINT:
    case CIR_IUINT:
        return 3;
    case CIR_ILONG:
    case CIR_IULONG:
        return 4;
    case CIR_ILONGLONG:
    case CIR_IULONGLONG:
        return 5;
    default:
        cir_bug("invalid ikind");
    }
}

const CirType *
CirType__arithmeticConversion(const CirType *t1, const CirType *t2, const CirMachine *mach)
{
    const CirType *t1u = CirType_unroll(t1);
    const CirType *t2u = CirType_unroll(t2);

    uint32_t t1_fkind = CirType_isFloat(t1u);
    uint32_t t2_fkind = CirType_isFloat(t2u);

    if (t1_fkind == CIR_FLONGDOUBLE) {
        return t1;
    } else if (t2_fkind == CIR_FLONGDOUBLE) {
        return t2;
    } else if (t1_fkind == CIR_FDOUBLE) {
        return t1;
    } else if (t2_fkind == CIR_FDOUBLE) {
        return t2;
    } else if (t1_fkind == CIR_FFLOAT) {
        return t1;
    } else if (t2_fkind == CIR_FFLOAT) {
        return t2;
    }

    t1 = CirType__integralPromotion(t1, mach);
    t2 = CirType__integralPromotion(t2, mach);
    t1u = CirType_unroll(t1);
    t2u = CirType_unroll(t2);
    uint32_t t1_ikind = CirType_isInt(t1u);
    uint32_t t2_ikind = CirType_isInt(t2u);

    // CirType__integralPromotion would have ensured they are ints
    assert(t1_ikind);
    assert(t2_ikind);

    // If both operands have the same type, then no further conversion is needed
    if (t1_ikind == t2_ikind) {
        return t1;
    }

    // Otherwise, if both operands have signed integer types
    // or both have unsigned integer types,
    // the operand with the type of lesser integer conversion rank is converted
    // to the type of the operand with greater rank.
    if (CirIkind_isSigned(t1_ikind, mach) == CirIkind_isSigned(t2_ikind, mach)) {
        assert(intRank(t1_ikind) != intRank(t2_ikind));
        return intRank(t1_ikind) < intRank(t2_ikind) ? t2 : t1;
    }

    // We need to know which one is signed for the next cases.
    uint32_t signed_ikind, unsigned_ikind;
    const CirType *signedt, *unsignedt;
    if (CirIkind_isSigned(t1_ikind, mach)) {
        signed_ikind = t1_ikind;
        unsigned_ikind = t2_ikind;
        signedt = t1;
        unsignedt = t2;
    } else {
        signed_ikind = t2_ikind;
        unsigned_ikind = t1_ikind;
        signedt = t2;
        unsignedt = t1;
    }

    // Otherwise, if the operand that has unsigned integer type has
    // rank greater of equal to the rank of the type of the other operand,
    // then the operand with signed integer type is converted to the type
    // of the operand with unsigned integer type.
    if (intRank(unsigned_ikind) >= intRank(signed_ikind)) {
        return unsignedt;
    }

    // Otherwise, if the type of the operand with signed integer type
    // can represent all of the values of the type of the operand
    // with unsigned integer type, then the operand with unsigned integer
    // type is converted to the type of the operand with signed integer type.
    if (CirIkind_size(signed_ikind, mach) > CirIkind_size(unsigned_ikind, mach)) {
        return signedt;
    }

    // Otherwise, both operands are converted to the unsigned integer type
    // corresponding to the type of the operand with signed integer type.
    return CirType_int(CirIkind_toUnsigned(signed_ikind));
}

// Rounds up `nrbits` to the nearest multiple of `roundto`.
// `roundto` must be a power of two.
static uint64_t
addTrailing(uint64_t nrbits, uint64_t roundto) {
    return (nrbits + roundto - 1) & (~(roundto - 1));
}

// Return alignment in bytes
uint64_t
CirType_alignof(const CirType *t, const CirMachine *mach)
{
    assert(t);
    assert(mach);
    uint32_t ikind = CirType_isInt(t);
    uint32_t fkind = CirType_isFloat(t);
    if (ikind == CIR_ICHAR || ikind == CIR_ISCHAR || ikind == CIR_IUCHAR) {
        return 1;
    } else if (ikind == CIR_IBOOL) {
        return mach->alignofBool;
    } else if (ikind == CIR_ISHORT || ikind == CIR_IUSHORT) {
        return mach->alignofShort;
    } else if (ikind == CIR_IINT || ikind == CIR_IUINT) {
        return mach->alignofInt;
    } else if (ikind == CIR_ILONG || ikind == CIR_IULONG) {
        return mach->alignofLong;
    } else if (ikind == CIR_ILONGLONG || ikind == CIR_IULONGLONG) {
        return mach->alignofLongLong;
    } else if (fkind == CIR_FFLOAT) {
        return mach->alignofFloat;
    } else if (fkind == CIR_FDOUBLE) {
        return mach->alignofDouble;
    } else if (fkind == CIR_FLONGDOUBLE) {
        return mach->alignofLongDouble;
    } else if (CirType_isNamed(t)) {
        CirTypedefId tid = CirType_getTypedefId(t);
        return CirType_alignof(CirTypedef_getType(tid), mach);
    } else if (CirType_isArray(t)) {
        return CirType_alignof(CirType_getBaseType(t), mach);
    } else if (CirType_isPtr(t) || CirType_isVaList(t)) {
        return mach->alignofPtr;
    } else if (CirType_isComp(t)) {
        CirCompId cid = CirType_getCompId(t);
        return CirComp_getAlign(cid, mach);
    } else if (CirType_isEnum(t)) {
        CirEnumId enumId = CirType_getEnumId(t);
        uint32_t ikind = CirEnum_getIkind(enumId);
        return CirType_alignof(CirType_int(ikind), mach);
    } else if (CirType_isFun(t)) {
        if (mach->compiler == CIR_GCC) {
            return mach->alignofFun;
        } else {
            cir_fatal("alignof called on function");
        }
    } else if (CirType_isVoid(t)) {
        cir_fatal("alignof called on void");
    } else {
        cir_bug("CirType_alignof: unhandled case");
    }
}

uint64_t
CirType_sizeof(const CirType *t, const CirMachine *mach)
{
    assert(t);
    assert(mach);
    uint32_t ikind = CirType_isInt(t);
    uint32_t fkind = CirType_isFloat(t);
    if (ikind) {
        return CirIkind_size(ikind, mach);
    } else if (fkind == CIR_FFLOAT) {
        return mach->sizeofFloat;
    } else if (fkind == CIR_FDOUBLE) {
        return mach->sizeofDouble;
    } else if (fkind == CIR_FLONGDOUBLE) {
        return mach->sizeofLongDouble;
    } else if (CirType_isPtr(t) || CirType_isVaList(t)) {
        return mach->sizeofPtr;
    } else if (CirType_isNamed(t)) {
        CirTypedefId tid = CirType_getTypedefId(t);
        return CirType_sizeof(CirTypedef_getType(tid), mach);
    } else if (CirType_isComp(t)) {
        CirCompId cid = CirType_getCompId(t);
        return CirComp_getSize(cid, mach);
    } else if (CirType_isEnum(t)) {
        CirEnumId enumId = CirType_getEnumId(t);
        uint32_t ikind = CirEnum_getIkind(enumId);
        return CirType_sizeof(CirType_int(ikind), mach);
    } else if (CirType_isArray(t)) {
        if (CirType_hasArrayLen(t)) {
            uint32_t len = CirType_getArrayLen(t);
            const CirType *bt = CirType_getBaseType(t);
            uint64_t size = CirType_sizeof(bt, mach) * len;
            return addTrailing(size, CirType_alignof(t, mach));
        } else {
            // It seems that on GCC the size of such an array is 0
            // TODO: REALLY?
            cir_fatal("CirType_bitsSizeOf: cannot take sizeof an array with no len");
        }
    } else if (CirType_isVoid(t)) {
        return mach->sizeofVoid;
    } else if (CirType_isFun(t)) {
        cir_fatal("Can't take sizeof a function");
    } else {
        cir_bug("CirType_bitsSizeOf: unhandled case");
    }
}

static const char *kindToStr[] = {
    [CIR_ICHAR] = "char",
    [CIR_ISCHAR] = "signed char",
    [CIR_IUCHAR] = "unsigned char",
    [CIR_IBOOL] = "_Bool",
    [CIR_IINT] = "int",
    [CIR_IUINT] = "unsigned",
    [CIR_ISHORT] = "short",
    [CIR_IUSHORT] = "unsigned short",
    [CIR_ILONG] = "long",
    [CIR_IULONG] = "unsigned long",
    [CIR_ILONGLONG] = "long long",
    [CIR_IULONGLONG] = "unsigned long long",
    [CIR_FFLOAT] = "float",
    [CIR_FDOUBLE] = "double",
    [CIR_FLONGDOUBLE] = "long double",
};

static void
CirType_printLhs(CirFmt printer, const CirType *t, bool needSpace)
{
    const CirAttr * const *attrs;
    size_t numAttrs;

    switch (data1ToType(t->data1)) {
    case CIR_TVOID:
        CirFmt_printString(printer, "void");
        goto finish;
    case CIR_TVALIST:
        CirFmt_printString(printer, "__builtin_va_list");
        goto finish;
    case CIR_TINT:
    case CIR_TFLOAT:
        CirFmt_printString(printer, kindToStr[data1ToU1(t->data1)]);
        goto finish;
    case CIR_TNAMED: {
        const CirTypeNamed *namedType = (const CirTypeNamed *)t;
        CirTypedefId tid = namedType->typedefId;
        CirFmt_printString(printer, "tid");
        CirFmt_printU32(printer, tid);
        CirFmt_printString(printer, "_");
        CirFmt_printString(printer, CirName_cstr(CirTypedef_getName(tid)));
        goto finish;
    }
    case CIR_TCOMP: {
        const CirTypeComp *compType = (const CirTypeComp *)t;
        CirCompId cid = compType->compId;
        CirFmt_printString(printer, CirComp_isStruct(cid) ? "struct cid" : "union cid");
        CirFmt_printU32(printer, cid);
        CirName name = CirComp_getName(cid);
        if (name) {
            CirFmt_printString(printer, "_");
            CirFmt_printString(printer, CirName_cstr(name));
        }
        goto finish;
    }
    case CIR_TENUM: {
        const CirTypeEnum *enumType = (const CirTypeEnum *)t;
        CirEnumId enumId = enumType->enumId;
        CirFmt_printString(printer, "enum eid");
        CirFmt_printU32(printer, enumId);
        CirName name = CirEnum_getName(enumId);
        if (name) {
            CirFmt_printString(printer, "_");
            CirFmt_printString(printer, CirName_cstr(name));
        }
        goto finish;
    }
    case CIR_TPTR: {
        const CirType *bt = CirType_getBaseType(t);
        bool needParen = CirType_isFun(bt) || CirType_isArray(bt);
        CirType_printLhs(printer, bt, true);

        CirFmt_printString(printer, needParen ? "(*" : "*");
        const CirAttr * const *attrs = CirType_getAttrs(t);
        size_t numAttrs = CirType_getNumAttrs(t);
        if (numAttrs) {
            CirAttr_printArray(printer, attrs, numAttrs);
            CirFmt_printString(printer, " ");
        }
        return;
    }
    case CIR_TARRAY:
    case CIR_TFUN: {
        const CirType *bt = CirType_getBaseType(t);
        CirType_printLhs(printer, bt, needSpace);
        return;
    }
    default:
        cir_bug("CirType__logLhs: unexpected type");
    }

finish:
    attrs = CirType_getAttrs(t);
    numAttrs = CirType_getNumAttrs(t);
    if (numAttrs) {
        CirFmt_printString(printer, " ");
        CirAttr_printArray(printer, attrs, numAttrs);
    }
    if (needSpace)
        CirFmt_printString(printer, " ");
}

static void
CirType_printRhs(CirFmt printer, const CirType *t, CirCodeId code_id, bool forRender)
{
loop:
    switch (data1ToType(t->data1)) {
    // Leaves
    case CIR_TVOID:
    case CIR_TVALIST:
    case CIR_TINT:
    case CIR_TFLOAT:
    case CIR_TNAMED:
    case CIR_TCOMP:
    case CIR_TENUM:
        return;

    case CIR_TPTR: {
        const CirType *bt = CirType_getBaseType(t);
        bool needParen = CirType_isFun(bt) || CirType_isArray(bt);
        if (needParen)
            CirFmt_printString(printer, ")");
        t = bt;
        goto loop;
    }
    case CIR_TARRAY: {
        if (CirType_hasArrayLen(t)) {
            uint32_t len = CirType_getArrayLen(t);
            CirFmt_printString(printer, "[");
            CirFmt_printU32(printer, len);
            CirFmt_printString(printer, "]");
        } else {
            CirFmt_printString(printer, "[]");
        }
        const CirType *bt = CirType_getBaseType(t);
        t = bt;
        goto loop;
    }
    case CIR_TFUN: {
        const CirType *bt = CirType_getBaseType(t);
        const CirFunParam *params = CirType_getParams(t);
        size_t numParams = CirType_getNumParams(t);
        bool isVa = CirType_isParamsVa(t);
        if (numParams) {
            CirFmt_printString(printer, "(");
            size_t i;
            for (i = 0; i < numParams; i++) {
                if (i)
                    CirFmt_printString(printer, ", ");
                if (code_id) {
                    CirVarId paramvar_id = CirCode_getVar(code_id, i);
                    CirVar_printDecl(printer, paramvar_id, forRender);
                } else {
                    CirType_print(printer, params[i].type, params[i].name ? CirName_cstr(params[i].name) : NULL, 0, forRender);
                }
            }
            if (isVa)
                CirFmt_printString(printer, i ? ", ..." : "...");
            CirFmt_printString(printer, ")");
        } else if (isVa) {
            CirFmt_printString(printer, "(...)");
        } else {
            CirFmt_printString(printer, "(void)");
        }
        t = bt;
        goto loop;
    }
    default:
        cir_bug("CirType__logRhs: unhandled type");
    }
}

void
CirType_print(CirFmt printer, const CirType *tt, const char *name, CirCodeId code_id, bool forRender)
{
    if (!name)
        name = "";

    CirType_printLhs(printer, tt, *name);
    CirFmt_printString(printer, name);
    CirType_printRhs(printer, tt, code_id, forRender);
}

void
CirType_log(const CirType *tt, const char *name)
{
    if (!tt) {
        CirLog_print("<CirType NULL>");
        return;
    }

    CirType_print(CirLog_printb, tt, name, 0, false);
}

bool
CirType_equals(const CirType *aType, const CirType *bType)
{
    assert(aType);
    assert(bType);

    if (aType == bType)
        return true;

    uint32_t aType_type = data1ToType(aType->data1);
    uint32_t bType_type = data1ToType(bType->data1);
    if (aType_type != bType_type)
        return false;

    // Need to recurse to structurally compare as needed
    // TODO: Compare attrs as well
    switch (aType_type) {
    case CIR_TVOID:
        return true;
    case CIR_TINT:
        return data1ToU1(aType->data1) == data1ToU1(bType->data1);
    case CIR_TFLOAT:
        return data1ToU1(aType->data1) == data1ToU1(bType->data1);
    case CIR_TPTR:
        return CirType_equals(CirType_getBaseType(aType), CirType_getBaseType(bType));
    case CIR_TARRAY:
        if (CirType_hasArrayLen(aType) != CirType_hasArrayLen(bType))
            return false;
        if (CirType_hasArrayLen(aType) && CirType_getArrayLen(aType) != CirType_getArrayLen(bType))
            return false;
        return CirType_equals(CirType_getBaseType(aType), CirType_getBaseType(bType));
    case CIR_TFUN: {
        if (CirType_getNumParams(aType) != CirType_getNumParams(bType))
            return false;
        if (CirType_isParamsVa(aType) != CirType_isParamsVa(bType))
            return false;
        size_t numParams = CirType_getNumParams(aType);
        const CirFunParam *aParams = CirType_getParams(aType);
        const CirFunParam *bParams = CirType_getParams(bType);
        for (size_t i = 0; i < numParams; i++) {
            if (!CirType_equals(aParams[i].type, bParams[i].type))
                return false;
        }
        return CirType_equals(CirType_getBaseType(aType), CirType_getBaseType(bType));
    }
    case CIR_TNAMED:
        return CirType_getTypedefId(aType) == CirType_getTypedefId(bType);
    case CIR_TCOMP:
        return CirType_getCompId(aType) == CirType_getCompId(bType);
    case CIR_TENUM:
        return CirType_getEnumId(aType) == CirType_getEnumId(bType);
    case CIR_TVALIST:
        return true;
    default:
        cir_bug("unhandled case");
    }
}

const CirType *
CirType__combine(const CirType *oldt, const CirType *t)
{
    uint32_t oldt_type = data1ToType(oldt->data1);
    uint32_t t_type = data1ToType(t->data1);
    const CirAttr * const *attrs = CirType_getAttrs(t);
    size_t attrs_len = CirType_getNumAttrs(t);
    const CirAttr * const *oldattrs = CirType_getAttrs(oldt);
    size_t oldattrs_len = CirType_getNumAttrs(oldt);

    if (oldt_type == CIR_TVOID && t_type == CIR_TVOID) {
        return CirType_withAttrs(oldt, attrs, attrs_len);
    } else if (oldt_type == CIR_TINT && t_type == CIR_TINT) {
        uint32_t oldikind = data1ToU1(oldt->data1);
        uint32_t ikind = data1ToU1(t->data1);
        if (oldikind != ikind)
            return NULL; // different integer types
        return CirType_withAttrs(oldt, attrs, attrs_len);
    } else if (oldt_type == CIR_TFLOAT && t_type == CIR_TFLOAT) {
        uint32_t oldfkind = data1ToU1(oldt->data1);
        uint32_t fkind = data1ToU1(t->data1);
        if (oldfkind != fkind)
            return NULL; // different floating point types
        return CirType_withAttrs(oldt, attrs, attrs_len);
    } else if (oldt_type == CIR_TENUM && t_type == CIR_TENUM) {
        CirEnumId enumId = CirType_getEnumId(t);
        const CirType *newT = CirType__enum(enumId, oldattrs, oldattrs_len);
        return CirType_withAttrs(newT, attrs, attrs_len);
    } else if (oldt_type == CIR_TCOMP && t_type == CIR_TCOMP) {
        CirCompId oldcid = ((const CirTypeComp *)oldt)->compId;
        CirCompId cid = ((const CirTypeComp *)t)->compId;
        if (CirComp_isStruct(oldcid) != CirComp_isStruct(cid))
            return NULL; // different struct/union types

        // Trivially the same
        if (oldcid == cid)
            return CirType_withAttrs(oldt, attrs, attrs_len);

        // We know they are the same
        if (CirComp__isIsomorphic(oldcid, cid))
            return CirType_withAttrs(oldt, attrs, attrs_len);

        // If one has 0 fields (undefined) while the other has some fields,
        // we accept it.
        size_t old_nrfields = CirComp_getNumFields(oldcid);
        size_t nrfields = CirComp_getNumFields(cid);
        if (old_nrfields == 0) {
            const CirType *base = CirType__comp(cid, oldattrs, oldattrs_len);
            return CirType_withAttrs(base, attrs, attrs_len);
        } else if (nrfields == 0) {
            return CirType_withAttrs(oldt, attrs, attrs_len);
        }

        // Make sure that at least they have the same number of fields
        if (old_nrfields != nrfields)
            return NULL; // different number of fields

        // Assume they are the same
        CirComp__markIsomorphic(oldcid, cid);

        // Check the fields are isomorphic and watch for failure
        for (size_t i = 0; i < nrfields; i++) {
            bool oldIsBitfield = CirComp_hasFieldBitsize(oldcid, i);
            bool isBitfield = CirComp_hasFieldBitsize(cid, i);
            if (oldIsBitfield && isBitfield && CirComp_getFieldBitsize(oldcid, i) != CirComp_getFieldBitsize(cid, i))
                goto comp_fail; // different bitfield size
            if (oldIsBitfield != isBitfield)
                goto comp_fail; // different bitfield info
            if (!CirType__combine(CirComp_getFieldType(oldcid, i), CirComp_getFieldType(cid, i)))
                goto comp_fail;
        }

        // We got here if we succeeded
        return CirType_withAttrs(oldt, attrs, attrs_len);

comp_fail:
        // Our assumption was wrong, forget the isomorphism.
        CirComp__unmarkIsomorphic(oldcid, cid);
        return NULL;
    } else if (oldt_type == CIR_TARRAY && t_type == CIR_TARRAY) {
        const CirType *newbt = CirType__combine(CirType_getBaseType(oldt), CirType_getBaseType(t));
        bool oldHasLen = CirType_hasArrayLen(oldt);
        bool newHasLen = CirType_hasArrayLen(t);
        bool needLen;
        uint32_t len;
        if (!oldHasLen && newHasLen) {
            needLen = true;
            len = CirType_getArrayLen(t);
        } else if (oldHasLen && !newHasLen) {
            needLen = true;
            len = CirType_getArrayLen(oldt);
        } else if (!oldHasLen && !newHasLen) {
            needLen = false;
        } else if (CirType_getArrayLen(oldt) == CirType_getArrayLen(t)) {
            needLen = true;
            len = CirType_getArrayLen(oldt);
        } else {
            return NULL; // different array lengths
        }
        const CirType *ret;
        if (needLen)
            ret = CirType__arrayWithLen(newbt, len, oldattrs, oldattrs_len);
        else
            ret = CirType__array(newbt, oldattrs, oldattrs_len);
        return CirType_withAttrs(ret, attrs, attrs_len);
    } else if (oldt_type == CIR_TPTR && t_type == CIR_TPTR) {
        const CirType *newbt = CirType__combine(CirType_getBaseType(oldt), CirType_getBaseType(t));
        const CirType *ret = CirType__ptr(newbt, oldattrs, oldattrs_len);
        return CirType_withAttrs(ret, attrs, attrs_len);
    } else if (oldt_type == CIR_TFUN && t_type == CIR_TFUN) {
        if (data1ToU2(oldt->data1) != data1ToU2(t->data1))
            return NULL; // different vararg specifiers
        if (CirType_getNumParams(oldt) != CirType_getNumParams(t))
            return NULL; // different number of arguments

        const CirType *newbt = CirType__combine(CirType_getBaseType(oldt), CirType_getBaseType(t));
        if (!newbt)
            return NULL;

        // Go over the arguments and update the names and types
        CirArray(CirFunParam) newParams = CIRARRAY_INIT;
        size_t numParams = CirType_getNumParams(t);
        const CirFunParam *oldparams = CirType_getParams(oldt);
        const CirFunParam *params = CirType_getParams(t);
        CirArray_alloc(&newParams, numParams);
        for (size_t i = 0; i < numParams; i++) {
            CirFunParam newParam;
            // Always prefer the new name.
            // This is very important if the prototype uses different names
            // than the function definition.
            newParam.name = params[i].name != 0 ? params[i].name : oldparams[i].name;
            newParam.type = CirType__combine(oldparams[i].type, params[i].type);
            if (!newParam.type)
                goto fun_fail;
        }

        const CirType *ret = CirType__fun(newbt, newParams.items, newParams.len, data1ToU2(oldt->data1), oldattrs, oldattrs_len);
        ret = CirType_withAttrs(ret, attrs, attrs_len);
        CirArray_release(&newParams);
        return ret;

fun_fail:
        CirArray_release(&newParams);
        return NULL;
    } else if (oldt_type == CIR_TNAMED && t_type == CIR_TNAMED && CirType_getTypedefId(oldt) == CirType_getTypedefId(t)) {
        return CirType_withAttrs(oldt, attrs, attrs_len);
    } else if (oldt_type == CIR_TVALIST && t_type == CIR_TVALIST) {
        return CirType_withAttrs(oldt, attrs, attrs_len);
    } else if (t_type == CIR_TNAMED) {
        // Unroll first the new type
        CirTypedefId tid = CirType_getTypedefId(t);
        const CirType *ret = CirType__combine(oldt, CirTypedef_getType(tid));
        if (!ret)
            return NULL;
        return CirType_withAttrs(ret, attrs, attrs_len);
    } else if (oldt_type == CIR_TNAMED) {
        // And unroll the old type as well if necessary
        CirTypedefId oldtid = CirType_getTypedefId(oldt);
        const CirType *ret = CirType__combine(CirTypedef_getType(oldtid), t);
        if (!ret)
            return NULL;
        return CirType_withAttrs(ret, attrs, attrs_len);
    } else {
        return NULL; // different type constructors
    }
}

CIR_PRIVATE
const CirType *
CirType_ofUnOp(uint32_t unop, const CirType *t1, const CirMachine *mach)
{
    switch (unop) {
    case CIR_UNOP_NEG: {
        const CirType *unrolledType = CirType_unroll(t1);
        if (CirType_isInt(unrolledType))
            return CirType__integralPromotion(t1, mach);
        else if (CirType_isFloat(unrolledType))
            return t1;
        else
            cir_fatal("CIR_UNOP_NEG: must have arithmetic type");
    }
    case CIR_UNOP_BNOT:
        return CirType__integralPromotion(t1, mach);
    case CIR_UNOP_LNOT:
        return CirType_int(CIR_IINT);
    case CIR_UNOP_ADDROF: {
        const CirType *unrolledType = CirType_unroll(t1);
        if (CirType_isArray(unrolledType) || CirType_isFun(unrolledType))
            return CirType_lvalConv(t1);
        else
            return CirType_ptr(t1);
    }
    case CIR_UNOP_IDENTITY:
        // CIR-specific unop: exact pass-through of the type
        return t1;
    default:
        cir_bug("unhandled unop");
    }
}

CIR_PRIVATE
const CirType *
CirType_ofBinOp(uint32_t binop, const CirType *lhs, const CirType *rhs, const CirMachine *mach)
{
    switch (binop) {
    case CIR_BINOP_PLUS: {
        lhs = CirType_lvalConv(lhs);
        rhs = CirType_lvalConv(rhs);
        const CirType *lhs_unrolled = CirType_unroll(lhs);
        const CirType *rhs_unrolled = CirType_unroll(rhs);
        if (CirType_isArithmetic(lhs_unrolled) && CirType_isArithmetic(rhs_unrolled)) {
            return CirType__arithmeticConversion(lhs, rhs, mach);
        } else if (CirType_isPtr(lhs_unrolled) && CirType_isInt(rhs_unrolled)) {
            return lhs;
        } else if (CirType_isInt(lhs_unrolled) && CirType_isPtr(rhs_unrolled)) {
            return rhs;
        } else {
            cir_fatal("CIR_BINOP_PLUS: operands have invalid type");
        }
    }
    case CIR_BINOP_MINUS: {
        lhs = CirType_lvalConv(lhs);
        rhs = CirType_lvalConv(rhs);
        const CirType *lhs_unrolled = CirType_unroll(lhs);
        const CirType *rhs_unrolled = CirType_unroll(rhs);
        if (CirType_isArithmetic(lhs_unrolled) && CirType_isArithmetic(rhs_unrolled)) {
            return CirType__arithmeticConversion(lhs, rhs, mach);
        } else if (CirType_isPtr(lhs_unrolled) && CirType_isInt(rhs_unrolled)) {
            return lhs;
        } else if (CirType_isPtr(lhs_unrolled) && CirType_isPtr(rhs_unrolled)) {
            return CirType_int(CirIkind_fromSize(mach->sizeofPtr, false, mach));
        } else {
            cir_fatal("CIR_BINOP_MINUS: operands have invalid type");
        }
    }
    case CIR_BINOP_MUL:
        return CirType__arithmeticConversion(lhs, rhs, mach);
    case CIR_BINOP_DIV:
        return CirType__arithmeticConversion(lhs, rhs, mach);
    case CIR_BINOP_MOD:
        return CirType__arithmeticConversion(lhs, rhs, mach);
    case CIR_BINOP_SHIFTLT:
        cir_bug("TODO: CIR_BINOP_SHIFTLT");
    case CIR_BINOP_SHIFTRT:
        cir_bug("TODO: CIR_BINOP_SHIFTRT");
    case CIR_BINOP_BAND:
        cir_bug("TODO: CIR_BINOP_BAND");
    case CIR_BINOP_BXOR:
        cir_bug("TODO: CIR_BINOP_BXOR");
    case CIR_BINOP_BOR:
        cir_bug("TODO: CIR_BINOP_BOR");
    default:
        cir_bug("unhandled binop");
    }
}

CIR_PRIVATE
const CirType *
CirType_ofCall(const CirType *targetType)
{
    const CirType *targetTypeUnrolled = CirType_unroll(targetType);
    if (CirType_isFun(targetTypeUnrolled)) {
        return CirType_getBaseType(targetTypeUnrolled);
    } else if (CirType_isPtr(targetTypeUnrolled)) {
        const CirType *bt = CirType_getBaseType(targetTypeUnrolled);
        bt = CirType_unroll(bt);
        if (!CirType_isFun(bt))
            goto fail;
        return CirType_getBaseType(bt);
    } else {
fail:
        CirLog_begin(CIRLOG_FATAL);
        CirLog_print("call: ");
        CirType_log(targetType, NULL);
        CirLog_print(" is not callable");
        CirLog_end();
        exit(1);
    }
}
