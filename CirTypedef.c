#include "cir_internal.h"
#include <assert.h>

#define MAX_TYPEDEFS 1024

// Reference-based struct, mutable
typedef struct CirTypedef {
    CirName name;
    const struct CirType *type;
} CirTypedef;

static CirTypedef typedefs[MAX_TYPEDEFS];
static uint32_t numTypedefs = 1;

CirTypedefId
CirTypedef_new(CirName name, const CirType *type)
{
    if (numTypedefs >= MAX_TYPEDEFS)
        cir_bug("too many typedefs");

    uint32_t tid = numTypedefs++;
    typedefs[tid].name = name;
    typedefs[tid].type = type;
    return tid;
}

CirName
CirTypedef_getName(CirTypedefId tid)
{
    return typedefs[tid].name;
}

const CirType *
CirTypedef_getType(CirTypedefId tid)
{
    assert(tid != 0);
    return typedefs[tid].type;
}

void
CirTypedef_log(CirTypedefId tid)
{
    if (tid == 0) {
        CirLog_print("<CirTypedef 0>");
        return;
    }
    CirLog_printf("tid%u_%s", (unsigned)tid, CirName_cstr(CirTypedef_getName(tid)));
}

size_t
CirTypedef_getNum(void)
{
    return numTypedefs;
}
