#include "cir_internal.h"
#include <assert.h>

#define MAX_ENUM_ITEMS (1024 * 4)

typedef struct CirEnumItem {
    CirName name;
    int64_t value;
} CirEnumItem;

static CirEnumItem enumItems[MAX_ENUM_ITEMS];
static uint32_t numEnumItems = 1;

CirEnumItemId
CirEnumItem_new(CirName name)
{
    if (!name)
        cir_bug("CirEnumItem_new: name must be non-zero");
    if (numEnumItems >= MAX_ENUM_ITEMS)
        cir_bug("too many enum items");

    CirEnumItemId enumItemId = numEnumItems++;
    enumItems[enumItemId].name = name;
    enumItems[enumItemId].value = 0;
    return enumItemId;
}

CirName
CirEnumItem_getName(CirEnumItemId enumItemId)
{
    assert(enumItemId);
    assert(enumItemId < numEnumItems);

    return enumItems[enumItemId].name;
}

int64_t
CirEnumItem_getI64(CirEnumItemId enumItemId)
{
    assert(enumItemId);
    assert(enumItemId < numEnumItems);

    return enumItems[enumItemId].value;
}

void
CirEnumItem_setI64(CirEnumItemId enumItemId, int64_t value)
{
    assert(enumItemId);
    assert(enumItemId < numEnumItems);

    enumItems[enumItemId].value = value;
}
