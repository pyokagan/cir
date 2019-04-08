#include "cir_internal.h"
#include <assert.h>

#define MAX_ENUMS (1024)

typedef struct CirEnum {
    CirName name; // optional
    uint32_t ikind;
    bool defined;
    CirArray(CirEnumItemId) enumItems;
} CirEnum;

static CirEnum enums[MAX_ENUMS];
static uint32_t numEnums = 1;

CirEnumId
CirEnum_new(void)
{
    if (numEnums >= MAX_ENUMS)
        cir_bug("too many enums");

    CirEnumId enumId = numEnums++;
    enums[enumId].name = 0;
    enums[enumId].ikind = CIR_IINT;
    enums[enumId].defined = false;
    CirArray_init(&enums[enumId].enumItems);
    return enumId;
}

CirName
CirEnum_getName(CirEnumId enumId)
{
    assert(enumId);
    assert(enumId < numEnums);

    return enums[enumId].name;
}

void
CirEnum_setName(CirEnumId enumId, CirName name)
{
    assert(enumId);
    assert(enumId < numEnums);

    enums[enumId].name = name;
}

size_t
CirEnum_getNumItems(CirEnumId enumId)
{
    assert(enumId);
    assert(enumId < numEnums);

    return enums[enumId].enumItems.len;
}

void
CirEnum_setNumItems(CirEnumId enumId, size_t newLen)
{
    assert(enumId);
    assert(enumId < numEnums);

    size_t origLen = enums[enumId].enumItems.len;
    CirArray_alloc(&enums[enumId].enumItems, newLen);
    for (size_t i = origLen; i < newLen; i++)
        enums[enumId].enumItems.items[i] = 0;
    enums[enumId].enumItems.len = newLen;
}

CirEnumItemId
CirEnum_getItem(CirEnumId enumId, size_t idx)
{
    assert(enumId);
    assert(enumId < numEnums);
    assert(idx < enums[enumId].enumItems.len);

    return enums[enumId].enumItems.items[idx];
}

void
CirEnum_setItem(CirEnumId enumId, size_t idx, CirEnumItemId enumItemId)
{
    assert(enumId);
    assert(enumId < numEnums);
    assert(idx < enums[enumId].enumItems.len);

    enums[enumId].enumItems.items[idx] = enumItemId;
}

uint32_t
CirEnum_getIkind(CirEnumId enumId)
{
    assert(enumId);
    assert(enumId < numEnums);

    return enums[enumId].ikind;
}

void
CirEnum_setIkind(CirEnumId enumId, uint32_t ikind)
{
    assert(enumId);
    assert(enumId < numEnums);

    enums[enumId].ikind = ikind;
}

bool
CirEnum_isDefined(CirEnumId enumId)
{
    assert(enumId);
    assert(enumId < numEnums);

    return enums[enumId].defined;
}

void
CirEnum_setDefined(CirEnumId enumId, bool defined)
{
    assert(enumId);
    assert(enumId < numEnums);

    enums[enumId].defined = defined;
}
