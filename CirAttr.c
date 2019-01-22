#include "cir_internal.h"
#include <assert.h>

#define CIRATTR_INT 1
#define CIRATTR_STR 2
#define CIRATTR_NAME 3
#define CIRATTR_CONS 4

// data1:
// bits 31 to 29: type
// bits: 28 to 0: numArgs

#define data1ToType(x) (((x) >> 29) & 0x07)
#define typeToData1(x) (((x) & 0x07) << 29)
#define data1ToNumArgs(x) ((x) & 0x1fffffff)
#define numArgsToData1(x) ((x) & 0x1fffffff)
#define MAX_CONS_ARGS 0x1fffffff

typedef struct CirAttr {
    uint32_t data1;
} CirAttr;

typedef struct CirAttrInt {
    uint32_t data1;
    int32_t i;
} CirAttrInt;

typedef struct CirAttrStr {
    uint32_t data1;
    const char *s;
} CirAttrStr;

typedef struct CirAttrName {
    uint32_t data1;
    CirName name;
} CirAttrName;

typedef struct CirAttrCons {
    uint32_t data1;
    CirName name;
    const CirAttr *args[];
} CirAttrCons;

const CirAttr *
CirAttr_int(int32_t i)
{
    CirAttrInt *attr = cir__balloc(sizeof(*attr));
    attr->data1 = typeToData1(CIRATTR_INT);
    attr->i = i;
    return (CirAttr *)attr;
}

const CirAttr *
CirAttr_str(const char *s)
{
    CirAttrStr *attr = cir__balloc(sizeof(*attr));
    attr->data1 = typeToData1(CIRATTR_STR);
    attr->s = s;
    return (CirAttr *)attr;
}

const CirAttr *
CirAttr_name(CirName name)
{
    CirAttrName *attr = cir__balloc(sizeof(*attr));
    attr->data1 = typeToData1(CIRATTR_NAME);
    attr->name = name;
    return (CirAttr *)attr;
}

const CirAttr *
CirAttr_cons(CirName name, const CirAttr **args, size_t len)
{
    if (len > MAX_CONS_ARGS)
        cir_bug("Too many attr args");

    CirAttrCons *attr = cir__balloc(sizeof(*attr) + sizeof(CirAttr *) * len);
    attr->data1 = typeToData1(CIRATTR_CONS) | numArgsToData1(len);
    attr->name = name;
    for (uint32_t i = 0; i < len; i++)
        attr->args[i] = args[i];
    return (CirAttr *)attr;
}

bool
CirAttr_isInt(const CirAttr *attr)
{
    return data1ToType(attr->data1) == CIRATTR_INT;
}

bool
CirAttr_isStr(const CirAttr *attr)
{
    return data1ToType(attr->data1) == CIRATTR_STR;
}

bool
CirAttr_isName(const CirAttr *attr)
{
    return data1ToType(attr->data1) == CIRATTR_NAME;
}

bool
CirAttr_isCons(const CirAttr *attr)
{
    return data1ToType(attr->data1) == CIRATTR_CONS;
}

CirName
CirAttr_getName(const CirAttr *attr)
{
    switch (data1ToType(attr->data1)) {
    case CIRATTR_NAME:
        return ((const CirAttrName *)attr)->name;
    case CIRATTR_CONS:
        return ((const CirAttrCons *)attr)->name;
    default:
        cir_bug("CirAttr_getName: not a name");
    }
}

size_t
CirAttr_getNumArgs(const CirAttr *attr)
{
    if (data1ToType(attr->data1) != CIRATTR_CONS)
        cir_bug("CirAttr_getNumArgs: attr is not cons type");
    return data1ToNumArgs(attr->data1);
}

const CirAttr * const *
CirAttr_getArgs(const CirAttr *attr)
{
    if (data1ToType(attr->data1) != CIRATTR_CONS)
        cir_bug("CirAttr_getArgs: attr is not cons type");
    return ((const CirAttrCons *)attr)->args;
}

void
CirAttrArray__add(CirAttrArray *arr, const CirAttr *item)
{
    CirArray_grow(arr, 1);
    CirName name = CirAttr_getName(item);

    size_t i;
    for (i = 0; i < arr->len; i++) {
        CirName itemName = CirAttr_getName(arr->items[i]);
        if (itemName == name) {
            // Do not add if already in there
            return;
        } else if (itemName > name) {
            break;
        }
    }
    for (size_t j = arr->len; j > i; j--) {
        arr->items[j] = arr->items[j - 1];
    }
    arr->items[i] = item;
    arr->len++;
}

void
CirAttrArray__merge(CirAttrArray *arr, const CirAttr * const *srcA, size_t lenA, const CirAttr * const *srcB, size_t lenB)
{
    size_t aIdx = 0, bIdx = 0, i = 0;

    CirArray_alloc(arr, lenA + lenB);
    while (aIdx < lenA && bIdx < lenB) {
        CirName nameA = CirAttr_getName(srcA[aIdx]);
        CirName nameB = CirAttr_getName(srcB[bIdx]);
        if (nameA < nameB) {
            // Choose nameA
            arr->items[i++] = srcA[aIdx];
            aIdx++;
        } else if (nameA == nameB) {
            // Choose nameA
            arr->items[i++] = srcA[aIdx];
            aIdx++;
            bIdx++;
        } else { // nameA > nameB
            // Choose nameB
            arr->items[i++] = srcB[bIdx];
            bIdx++;
        }
    }
    while (aIdx < lenA)
        arr->items[i++] = srcA[aIdx++];
    while (bIdx < lenB)
        arr->items[i++] = srcB[bIdx++];
    arr->len = i;
}

void
CirAttrArray__remove(CirAttrArray *arr, const CirAttr *const *srcA, size_t lenA, const CirAttr *const *removeB, size_t lenB)
{
    size_t aIdx = 0, bIdx = 0, i = 0;

    CirArray_alloc(arr, lenA);
    while (aIdx < lenA && bIdx < lenB) {
        CirName nameA = CirAttr_getName(srcA[aIdx]);
        CirName nameB = CirAttr_getName(removeB[bIdx]);
        if (nameA < nameB) {
            // Can add nameA
            arr->items[i++] = srcA[aIdx];
            aIdx++;
        } else if (nameA == nameB) {
            // Don't add nameA
            aIdx++;
        } else { // nameA > nameB
            bIdx++;
        }
    }
    // Add the rest
    while (aIdx < lenA)
        arr->items[i++] = srcA[aIdx++];
    assert(i <= arr->alloc);
    arr->len = i;
}

static bool
isNameAttr(CirName name)
{
    assert(name);
    const char *n = CirName_cstr(name);
    return !strcmp(n, "section") ||
        !strcmp(n, "constructor") ||
        !strcmp(n, "destructor") ||
        !strcmp(n, "unused") ||
        !strcmp(n, "used") ||
        !strcmp(n, "weak") ||
        !strcmp(n, "no_instrument_function") ||
        !strcmp(n, "alias") ||
        !strcmp(n, "no_check_memory_usage") ||
        !strcmp(n, "exception") ||
        !strcmp(n, "__asm__");
}

static bool
isFunAttr(CirName name)
{
    assert(name);
    const char *n = CirName_cstr(name);
    return !strcmp(n, "format") ||
        !strcmp(n, "regparm") ||
        !strcmp(n, "longcall") ||
        !strcmp(n, "noinline") ||
        !strcmp(n, "always_inline") ||
        !strcmp(n, "gnu_inline") ||
        !strcmp(n, "leaf") ||
        !strcmp(n, "artificial") ||
        !strcmp(n, "warn_unused_result") ||
        !strcmp(n, "nonnull");
}

static bool
isTypeAttr(CirName name)
{
    assert(name);
    const char *n = CirName_cstr(name);
    return !strcmp(n, "const") ||
        !strcmp(n, "volatile") ||
        !strcmp(n, "restrict") ||
        !strcmp(n, "mode");
}

static bool
isPrintOutsideAttr(CirName name)
{
    assert(name);
    const char *n = CirName_cstr(name);
    return !strcmp(n, "const") ||
        !strcmp(n, "volatile") ||
        !strcmp(n, "__asm__") ||
        !strcmp(n, "restrict");
}

void
CirAttr__partition(const CirAttr * const *attrs, size_t numAttrs, CirAttrArray *outName, CirAttrArray *outFun, CirAttrArray *outType, int _default)
{
    for (size_t i = 0; i < numAttrs; i++) {
        CirName name = CirAttr_getName(attrs[i]);
        if (isNameAttr(name)) {
            CirArray_push(outName, &attrs[i]);
        } else if (isFunAttr(name)) {
            CirArray_push(outFun, &attrs[i]);
        } else if (isTypeAttr(name)) {
            CirArray_push(outType, &attrs[i]);
        } else if (_default == CIRATTR_PARTITION_DEFAULT_NAME) {
            CirArray_push(outName, &attrs[i]);
        } else if (_default == CIRATTR_PARTITION_DEFAULT_FUN) {
            CirArray_push(outFun, &attrs[i]);
        } else if (_default == CIRATTR_PARTITION_DEFAULT_TYPE) {
            CirArray_push(outType, &attrs[i]);
        } else {
            cir_bug("CirAttr__partition: invalid default");
        }
    }
}

static const char *
mapName(CirName name)
{
    assert(name);
    const char *n = CirName_cstr(name);
    if (!strcmp(n, "restrict")) {
        return "__restrict";
    } else {
        return n;
    }
}

static void
CirAttr_print(CirFmt printer, const CirAttr *attr)
{
    switch (data1ToType(attr->data1)) {
    case CIRATTR_INT:
        CirFmt_printI32(printer, ((const CirAttrInt *)attr)->i);
        break;
    case CIRATTR_STR:
        CirFmt_printString(printer, ((const CirAttrStr *)attr)->s);
        break;
    case CIRATTR_NAME:
        CirFmt_printString(printer, mapName(((const CirAttrName *)attr)->name));
        break;
    case CIRATTR_CONS:
        CirFmt_printString(printer, mapName(((const CirAttrCons *)attr)->name));
        CirFmt_printString(printer, "(");
        size_t numArgs = CirAttr_getNumArgs(attr);
        const CirAttr * const *args = CirAttr_getArgs(attr);
        for (size_t i = 0; i < numArgs; i++) {
            if (i)
                CirFmt_printString(printer, ", ");
            CirAttr_print(printer, args[i]);
        }
        CirFmt_printString(printer, ")");
        break;
    default:
        cir_bug("CirAttr_log: unhandled attr type");
    }
}

void
CirAttr_log(const CirAttr *attr)
{
    CirAttr_print(CirLog_printb, attr);
}

void
CirAttr_printArray(CirFmt printer, const CirAttr * const *attrs, size_t numAttrs)
{
    // First pass: attributes which should not be in the attrs block
    bool printSpace = false;
    for (size_t i = 0; i < numAttrs; i++) {
        CirName name = CirAttr_getName(attrs[i]);
        if (isPrintOutsideAttr(name)) {
            if (printSpace)
                CirFmt_printString(printer, " ");
            CirAttr_print(printer, attrs[i]);
            printSpace = true;
        }
    }

    // Second pass: attributes which should be in the attrs block
    bool printComma = false;
    for (size_t i = 0; i < numAttrs; i++) {
        CirName name = CirAttr_getName(attrs[i]);
        if (isPrintOutsideAttr(name)) {
            // do nothing
        } else {
            if (printComma) {
                CirFmt_printString(printer, ", ");
            } else {
                if (printSpace)
                    CirFmt_printString(printer, " ");
                CirFmt_printString(printer, "__attribute__((");
            }
            printComma = true;
            CirAttr_print(printer, attrs[i]);
        }
    }
    if (printComma)
        CirFmt_printString(printer, "))");
}

void
CirAttr__logArray(const CirAttr *const *attrs, size_t numAttrs)
{
    CirAttr_printArray(CirLog_printb, attrs, numAttrs);
}
