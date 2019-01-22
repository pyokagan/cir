#include "cir_internal.h"
#include <assert.h>
#include <stdlib.h>

// data1:
// bits 31 to 29: type
// bits 28 to 23: number of field offsets (6 bits) [u1]

// type (3 bits)
#define CIRVALUE_INT 1
#define CIRVALUE_STR 2
#define CIRVALUE_VAR 3
#define CIRVALUE_MEM 4

#define MAX_FIELDS 63

#define data1ToType(x) (((x) >> 29) & 0x07)
#define typeToData1(x) (((x) & 0x07) << 29)
#define data1ToU1(x) (((x) >> 23) & 0x3f)
#define u1ToData1(x) (((x) & 0x3f) << 23)

// Data layouts
typedef struct CirValue {
    uint32_t data1;
} CirValue;

typedef struct CirValueInt {
    uint32_t data1;
    const CirType *type;
    union {
        uint64_t u64;
        int64_t i64;
    } val;
} CirValueInt;

typedef struct CirValueStr {
    uint32_t data1;
    uint32_t len;
    const CirType *type;
    const char *s;
} CirValueStr;

typedef struct CirValueVar {
    uint32_t data1;
    CirVarId vid;
    const CirType *type;
    CirName fields[];
} CirValueVar;

static const CirValue *
makeIntValue(uint64_t val, const CirType *type)
{
    assert(type); // Int values by themselves are untyped -- we need a type
    CirValueInt *out = cir__balloc(sizeof(*out));
    out->data1 = typeToData1(CIRVALUE_INT);
    out->val.u64 = val;
    out->type = type;
    return (CirValue *)out;
}

static const CirValue *
makeStrValue(const char *s, uint32_t len, const CirType *type)
{
    assert(s);
    CirValueStr *out = cir__balloc(sizeof(*out));
    out->data1 = typeToData1(CIRVALUE_STR);
    out->len = len;
    out->type = type;
    out->s = s;
    return (CirValue *)out;
}

static const CirValue *
makeVarValue(CirVarId vid, uint32_t tt, const CirName *fields, size_t numFields, const CirType *type)
{
    if (numFields > MAX_FIELDS)
        cir_bug("too many fields");
    CirValueVar *out = cir__balloc(sizeof(*out) + sizeof(*fields) * numFields);
    out->data1 = typeToData1(tt) | u1ToData1(numFields);
    out->vid = vid;
    out->type = type;
    for (size_t i = 0; i < numFields; i++)
        out->fields[i] = fields[i];
    return (CirValue *)out;
}

const CirValue *
CirValue_ofU64(uint32_t ikind, uint64_t val)
{
    return makeIntValue(val, CirType_int(ikind));
}

const CirValue *
CirValue_ofI64(uint32_t ikind, int64_t val)
{
    union { uint64_t u64; int64_t i64; } u;
    u.i64 = val;
    return makeIntValue(u.u64, CirType_int(ikind));
}

const CirValue *
CirValue_ofVar(CirVarId vid)
{
    return makeVarValue(vid, CIRVALUE_VAR, NULL, 0, NULL);
}

const CirValue *
CirValue_ofMem(CirVarId vid)
{
    return makeVarValue(vid, CIRVALUE_MEM, NULL, 0, NULL);
}

const CirValue *
CirValue_ofString(const char *str, size_t len)
{
    if (len > (uint32_t)-1)
        cir_fatal("string is too long: %llu", (unsigned long long)len);
    return makeStrValue(str, len, NULL /* no cast -- use type of string */);
}

const CirValue *
CirValue_ofCString(const char *str)
{
    return CirValue_ofString(str, strlen(str) + 1);
}

bool
CirValue_isInt(const CirValue *value)
{
    assert(value != NULL);
    return data1ToType(value->data1) == CIRVALUE_INT;
}

bool
CirValue_isString(const CirValue *value)
{
    assert(value != NULL);
    return data1ToType(value->data1) == CIRVALUE_STR;
}

bool
CirValue_isVar(const CirValue *value)
{
    assert(value != NULL);
    return data1ToType(value->data1) == CIRVALUE_VAR;
}

bool
CirValue_isMem(const CirValue *value)
{
    assert(value != NULL);
    return data1ToType(value->data1) == CIRVALUE_MEM;
}

bool
CirValue_isLval(const CirValue *value)
{
    return CirValue_isVar(value) || CirValue_isMem(value);
}

uint64_t
CirValue_getU64(const CirValue *value)
{
    if (!CirValue_isInt(value))
        cir_bug("CirValue_getU64: not an int var");
    const CirValueInt *intValue = (const CirValueInt *)value;
    return intValue->val.u64;
}

int64_t
CirValue_getI64(const CirValue *value)
{
    if (!CirValue_isInt(value))
        cir_bug("CirValue_getI64: not an int var");
    const CirValueInt *intValue = (const CirValueInt *)value;
    return intValue->val.i64;
}

const char *
CirValue_getString(const CirValue *value)
{
    if (!CirValue_isString(value))
        cir_bug("CirValue_getString: not a string var");
    const CirValueStr *strValue = (const CirValueStr *)value;
    return strValue->s;
}

size_t
CirValue_getNumFields(const CirValue *value)
{
    if (!CirValue_isLval(value))
        cir_bug("CirValue_getNumFields: not an lval");
    return data1ToU1(value->data1);
}

CirName
CirValue_getField(const CirValue *value, size_t i)
{
    if (!CirValue_isLval(value))
        cir_bug("CirValue_getField: not an lval");
    size_t numFields = CirValue_getNumFields(value);
    if (i >= numFields)
        cir_bug("CirValue_getField: index out of bounds");
    const CirValueVar *varValue = (const CirValueVar *)value;
    return varValue->fields[i];
}

const CirValue *
CirValue_withFields(const CirValue *value, const CirName *fields, size_t len)
{
    if (!CirValue_isLval(value))
        cir_bug("CirValue_withFields: not an lval");
    const CirValueVar *varValue = (const CirValueVar *)value;
    size_t numFields = CirValue_getNumFields(value);
    CirValueVar *out = (CirValueVar *)makeVarValue(varValue->vid, data1ToType(value->data1), varValue->fields, numFields + len, varValue->type);
    for (size_t i = 0; i < len; i++)
        out->fields[numFields + i] = fields[i];
    return (const CirValue *)out;
}

CirVarId
CirValue_getVar(const CirValue *value)
{
    if (!CirValue_isLval(value))
        cir_bug("CirValue_getVar: not an lval");
    const CirValueVar *varValue = (const CirValueVar *)value;
    return varValue->vid;
}

const CirType *
CirValue_computeTypeAndBitsOffset(const CirValue *value, uint64_t *offset, const CirMachine *mach)
{
    assert(value != NULL);
    uint32_t tt = data1ToType(value->data1);
    switch (tt) {
    case CIRVALUE_INT:
        return CirValue_getCastType(value);
    case CIRVALUE_STR: {
        const CirValueStr *strValue = (const CirValueStr *)value;
        const CirType *castType = CirValue_getCastType(value);
        if (castType)
            return castType;
        else
            return CirType_arrayWithLen(CirType_int(CIR_ICHAR), strValue->len);
    }
    case CIRVALUE_VAR:
    case CIRVALUE_MEM: {
        const CirValueVar *varValue = (const CirValueVar *)value;
        const CirType *type = CirVar_getType(varValue->vid);
        uint64_t totalOffset = 0;
        if (!type) {
            if (offset)
                *offset = (uint64_t)-1;
            return CirValue_getCastType(value);
        }
        if (tt == CIRVALUE_MEM) {
            type = CirType_unroll(type);
            if (!CirType_isPtr(type)) {
                CirLog_begin(CIRLOG_FATAL);
                CirLog_print("error while computing type of ");
                CirValue_log(value);
                CirLog_print(": not a pointer type: ");
                CirType_log(type, NULL);
                CirLog_end();
                exit(1);
            }
            type = CirType_getBaseType(type);
        }
        size_t numFields = CirValue_getNumFields(value);
        for (size_t i = 0; i < numFields; i++) {
            CirName fieldName = CirValue_getField(value, i);
            if (!type && !offset)
                return NULL;
            type = type ? CirType_unroll(type) : NULL;
            if (!type || !CirType_isComp(type)) {
                CirLog_begin(CIRLOG_FATAL);
                CirLog_printf("error while computing type of field %llu of ", (unsigned long long)i);
                CirValue_log(value);
                CirLog_print(": not a comp type: ");
                CirType_log(type, NULL);
                CirLog_end();
                exit(1);
            }
            CirCompId comp_id = CirType_getCompId(type);
            size_t fieldIdx;
            if (!CirComp_getFieldByName(comp_id, fieldName, &fieldIdx)) {
                CirLog_begin(CIRLOG_FATAL);
                CirLog_printf("error while computing type of field %llu of ", (unsigned long long)i);
                CirValue_log(value);
                CirLog_printf(": could not find field %s in: ", CirName_cstr(fieldName));
                CirType_log(type, NULL);
                CirLog_end();
                exit(1);
            }
            type = CirComp_getFieldType(comp_id, fieldIdx);
            if (offset)
                totalOffset += CirComp_getFieldBitsOffset(comp_id, fieldIdx, mach);
        }
        if (offset)
            *offset = totalOffset;
        return CirValue_getCastType(value) ? CirValue_getCastType(value) : type;
    }
    default:
        cir_bug("CirValue_getType: unhandled type");
    }
}

uint64_t
CirValue_computeBitsOffset(const CirValue *value, const CirMachine *mach)
{
    uint64_t offset = 0;
    CirValue_computeTypeAndBitsOffset(value, &offset, mach);
    if (offset == (uint64_t)-1)
        cir_fatal("could not compute offset of unknown type");
    return offset;
}

const CirType *
CirValue_getType(const CirValue *value)
{
    return CirValue_computeTypeAndBitsOffset(value, NULL, NULL);
}

const CirType *
CirValue_getCastType(const CirValue *value)
{
    assert(value != NULL);
    switch (data1ToType(value->data1)) {
    case CIRVALUE_INT: {
        const CirValueInt *intValue = (const CirValueInt *)value;
        assert(intValue->type);
        return intValue->type;
    }
    case CIRVALUE_STR: {
        const CirValueStr *strValue = (const CirValueStr *)value;
        return strValue->type;
    }
    case CIRVALUE_VAR:
    case CIRVALUE_MEM: {
        const CirValueVar *varValue = (const CirValueVar *)value;
        return varValue->type;
    }
    default:
        cir_bug("unreachable");
    }
}

const CirValue *
CirValue_withCastType(const CirValue *value, const CirType *castType)
{
    assert(value != NULL);
    uint32_t tt = data1ToType(value->data1);
    switch (tt) {
    case CIRVALUE_INT: {
        const CirValueInt *intValue = (const CirValueInt *)value;
        if (!castType)
            cir_fatal("cannot set castType to NULL for int value");
        return makeIntValue(intValue->val.u64, castType);
    }
    case CIRVALUE_STR: {
        const CirValueStr *strValue = (const CirValueStr *)value;
        return makeStrValue(strValue->s, strValue->len, castType);
    }
    case CIRVALUE_VAR:
    case CIRVALUE_MEM: {
        const CirValueVar *varValue = (const CirValueVar *)value;
        return makeVarValue(varValue->vid, tt, varValue->fields, CirValue_getNumFields(value), castType);
    }
    default:
        cir_bug("unreachable");
    }
}

void
CirValue_print(CirFmt printer, const CirValue *value, bool renderName)
{
    if (!value) {
        CirFmt_printString(printer, "<CirValue NULL>");
        return;
    }

    switch (data1ToType(value->data1)) {
    case CIRVALUE_INT: {
        const CirValueInt *intValue = (const CirValueInt *)value;
        assert(intValue->type);
        // Print cast
        CirFmt_printString(printer, "(");
        CirType_print(printer, intValue->type, "", 0, renderName);
        CirFmt_printString(printer, ")");
        // Print literal (note: type could be a pointer)
        uint32_t ikind = CirType_isInt(CirType_unroll(intValue->type));
        if (ikind && CirIkind_isSigned(ikind, &CirMachine__host))
            CirFmt_printI64(printer, intValue->val.i64);
        else
            CirFmt_printU64(printer, intValue->val.u64);
        break;
    }
    case CIRVALUE_STR: {
        const CirValueStr *strValue = (const CirValueStr *)value;
        if (strValue->type) {
            // Print cast
            CirFmt_printString(printer, "(");
            CirType_print(printer, strValue->type, "", 0, renderName);
            CirFmt_printString(printer, ")");
        }
        bool hasNulByte = false;
        size_t len = strValue->len;
        if (len && strValue->s[len - 1] == '\0') {
            hasNulByte = true;
            len--;
        }
        CirFmt_printqb(printer, strValue->s, len);
        if (!hasNulByte)
            CirFmt_printString(printer, "/* NONUL */");
        break;
    }
    case CIRVALUE_VAR: {
        const CirValueVar *varValue = (const CirValueVar *)value;
        if (varValue->type) {
            // Print cast
            CirFmt_printString(printer, "(");
            CirType_print(printer, varValue->type, "", 0, renderName);
            CirFmt_printString(printer, ")");
        }
        size_t numFields = CirValue_getNumFields(value);
        CirVar_printLval(printer, varValue->vid, renderName);
        for (size_t i = 0; i < numFields; i++) {
            CirFmt_printString(printer, ".");
            CirFmt_printString(printer, CirName_cstr(varValue->fields[i]));
        }
        break;
    }
    case CIRVALUE_MEM: {
        const CirValueVar *varValue = (const CirValueVar *)value;
        if (varValue->type) {
            // Print cast
            CirFmt_printString(printer, "(");
            CirType_print(printer, varValue->type, "", 0, renderName);
            CirFmt_printString(printer, ")");
        }
        size_t numFields = CirValue_getNumFields(value);
        if (!numFields)
            CirFmt_printString(printer, "*");
        CirVar_printLval(printer, varValue->vid, renderName);
        for (size_t i = 0; i < numFields; i++) {
            CirFmt_printString(printer, i ? "." : "->");
            CirFmt_printString(printer, CirName_cstr(varValue->fields[i]));
        }
        break;
    }
    default:
        cir_bug("CirValue_log: unhandled value type");
    }
}

void
CirValue_log(const CirValue *value)
{
    CirValue_print(CirLog_printb, value, false);
}
