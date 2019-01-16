#include "cir_internal.h"
#include <assert.h>
#include <stdio.h>

#define MAX_VARS 1024

// data1:
// bits 31 to 29: storage

#define data1ToStorage(x) (((x) >> 29) & 0x07)
#define storageToData1(x) (((x) & 0x07) << 29)
#define data1ClearStorage(x) ((x) & ~(0x07 << 29))

// Reference-based struct, mutable
typedef struct CirVar {
    CirName name;
    uint32_t data1; // storage, inline, autotyp, num attrs
    const CirType *type;
    CirCodeId code;
    CirCodeId owner;
    CirArray(CirVarId) formals;
} CirVar;

static CirVar vars[MAX_VARS];
static uint32_t numVars = 1;

__attribute__((unused)) static bool
isStorage(uint32_t x)
{
    switch (x) {
    case CIR_NOSTORAGE:
    case CIR_STATIC:
    case CIR_REGISTER:
    case CIR_EXTERN:
        return true;
    default:
        return false;
    }
}

CirVarId
CirVar_new(CirCodeId code_id)
{
    if (numVars >= MAX_VARS)
        cir_bug("too many vars");

    CirVarId var_id = numVars++;
    vars[var_id].data1 = 0;
    vars[var_id].owner = code_id;
    if (code_id) {
        CirCode__addVar(code_id, var_id);
    }

    return var_id;
}

CirName
CirVar_getName(CirVarId vid)
{
    assert(vid != 0);
    return vars[vid].name;
}

void
CirVar_setName(CirVarId vid, CirName name)
{
    assert(vid != 0);
    vars[vid].name = name;
}

const CirType *
CirVar_getType(CirVarId vid)
{
    assert(vid != 0);
    return vars[vid].type;
}

void
CirVar_setType(CirVarId vid, const CirType *t)
{
    assert(vid != 0);
    vars[vid].type = t;

    // Ensure the number of formals matches the number of function args in type
    if (CirType_isFun(t)) {
        size_t numArgs = CirType_getNumParams(t);
        CirArray_alloc(&vars[vid].formals, numArgs);
        for (size_t i = vars[vid].formals.len; i < numArgs; i++)
            vars[vid].formals.items[i] = 0;
        vars[vid].formals.len = numArgs;
    }
}

CirCodeId
CirVar_getCode(CirVarId var_id)
{
    assert(var_id != 0);

    return vars[var_id].code;
}

void
CirVar__setCode(CirVarId var_id, CirCodeId code_id)
{
    assert(var_id != 0);
    assert(vars[var_id].type != NULL);
    assert(CirType_isFun(vars[var_id].type));
    assert(code_id != 0);
    vars[var_id].code = code_id;
}

void
CirVar__setOwner(CirVarId var_id, CirCodeId code_id)
{
    assert(var_id != 0);
    vars[var_id].owner = code_id;
}

CirCodeId
CirVar_getOwner(CirVarId var_id)
{
    assert(var_id != 0);
    return vars[var_id].owner;
}

CirVarId
CirVar_getFormal(CirVarId var_id, size_t i)
{
    assert(var_id != 0);
    assert(i < vars[var_id].formals.len);
    return vars[var_id].formals.items[i];
}

void
CirVar_setFormal(CirVarId var_id, size_t i, CirVarId formal_id)
{
    assert(var_id != 0);
    assert(i < vars[var_id].formals.len);
    vars[var_id].formals.items[i] = formal_id;
}

const CirVarId *
CirVar__getFormals(CirVarId var_id)
{
    assert(var_id != 0);
    return vars[var_id].formals.items;
}

CirStorage
CirVar_getStorage(CirVarId vid)
{
    assert(vid != 0);
    return data1ToStorage(vars[vid].data1);
}

void
CirVar_setStorage(CirVarId vid, CirStorage storage)
{
    if (!isStorage(storage))
        cir_fatal("CirVar_setStorage: not a valid storage");

    vars[vid].data1 = data1ClearStorage(vars[vid].data1) | storageToData1(storage);
}

void
CirVar_printLval(CirFmt printer, CirVarId var_id, bool forRender)
{
    CirName name = CirVar_getName(var_id);
    CirStorage storage = CirVar_getStorage(var_id);
    bool printVid = true;
    if (forRender && storage != CIR_STATIC && !CirVar_getOwner(var_id))
        printVid = false;
    if (printVid) {
        CirFmt_printString(printer, "vid");
        CirFmt_printU32(printer, var_id);
    }
    if (name) {
        if (printVid)
            CirFmt_printString(printer, "_");
        CirFmt_printString(printer, CirName_cstr(name));
    }
}

void
CirVar_printDecl(CirFmt printer, CirVarId var_id, bool forRender)
{
    char namebuf[64];

    CirName name = CirVar_getName(var_id);
    CirStorage storage = CirVar_getStorage(var_id);
    switch (storage) {
    case CIR_STATIC:
        CirFmt_printString(printer, "static ");
        break;
    case CIR_REGISTER:
        CirFmt_printString(printer, "register ");
        break;
    case CIR_EXTERN:
        CirFmt_printString(printer, "extern ");
        break;
    }
    bool printVid = true;
    if (forRender && storage != CIR_STATIC && !CirVar_getOwner(var_id))
        printVid = false;
    if (printVid) {
        snprintf(namebuf, sizeof(namebuf), "vid%u%s%s", (unsigned)var_id,
            name ? "_" : "", name ? CirName_cstr(name) : "");
    } else {
        snprintf(namebuf, sizeof(namebuf), "%s", CirName_cstr(name));
    }
    const CirType *type = CirVar_getType(var_id);
    CirCodeId code_id = CirVar_getCode(var_id);
    CirType_print(printer, type, namebuf, code_id, forRender);
}

void
CirVar_log(CirVarId vid)
{
    if (vid == 0) {
        CirLog_print("<CirVar 0>");
        return;
    }
    CirVar_printLval(CirLog_printb, vid, false);
}

void
CirVar_logNameAndType(CirVarId vid)
{
    char strName[64] = {};

    if (vid == 0) {
        CirLog_print("<CirVar 0>");
        return;
    }
    CirName name = CirVar_getName(vid);
    snprintf(strName, sizeof(strName), "vid%u%s%s", (unsigned)vid, name ? "_" : "", name ? CirName_cstr(name) : "");
    const CirType *type = CirVar_getType(vid);
    if (type) {
        CirType_log(type, strName);
    } else {
        CirLog_print("__auto_type ");
        CirLog_print(strName);
    }
}

size_t
CirVar_getNum(void)
{
    return numVars;
}
