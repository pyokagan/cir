#include "cir_internal.h"
#include <assert.h>

#define TABLE_SIZE 503
#define GLOBAL_TABLE_SIZE 5303
//#define TABLE_SIZE 2111

typedef struct NameItem {
    CirName key;
    enum {
        NAME_ITEM_VAR,
        NAME_ITEM_TYPEDEF,
        NAME_ITEM_ENUM
    } type;
    union {
        CirVarId varId;
        CirTypedefId typedefId;
        CirEnumItemId enumItemId;
    } value;
    CirVarId varId;
} NameItem;

typedef struct TagItem {
    CirName key;
    bool isEnum;
    union {
        CirCompId compId;
        CirEnumId enumId;
    } u;
} TagItem;

typedef struct Scope {
    NameItem *names;
    TagItem *tags;
    size_t tableSize;
} Scope;

static Scope scopes[CIR_MAX_SCOPES];
static uint32_t scopeStackTop;

static const NameItem *
findNameItem(const Scope *scope, CirName name)
{
    size_t tableSize = scope->tableSize;
    for (uint32_t i = name % tableSize; scope->names[i].key; i = (i + 1) % tableSize) {
        if (scope->names[i].key == name) {
            return &scope->names[i];
        }
    }

    return NULL;
}

static void
replaceNameItem(Scope *scope, const NameItem *item)
{
    uint32_t i;
    size_t tableSize = scope->tableSize;
    for (i = item->key % tableSize; scope->names[i].key && scope->names[i].key != item->key; i = (i + 1) % tableSize);
    scope->names[i] = *item;
}

static const TagItem *
findTagItem(const Scope *scope, CirName name)
{
    size_t tableSize = scope->tableSize;
    for (uint32_t i = name % tableSize; scope->tags[i].key; i = (i + 1) % tableSize) {
        if (scope->tags[i].key == name) {
            return &scope->tags[i];
        }
    }

    return NULL;
}

static void
replaceTagItem(Scope *scope, const TagItem *item)
{
    uint32_t i;
    size_t tableSize = scope->tableSize;
    for (i = item->key % tableSize; scope->tags[i].key && scope->tags[i].key != item->key; i = (i + 1) % tableSize);
    scope->tags[i] = *item;
}

void
CirEnv__pushScope(size_t tableSize)
{
    if (scopeStackTop >= CIR_MAX_SCOPES)
        cir_fatal("too many nested scopes");

    scopes[scopeStackTop].names = cir__zalloc(sizeof(NameItem) * tableSize);
    scopes[scopeStackTop].tags = cir__zalloc(sizeof(TagItem) * tableSize);
    scopes[scopeStackTop].tableSize = tableSize;
    scopeStackTop++;
}

void
CirEnv__pushGlobalScope(void)
{
    CirEnv__pushScope(GLOBAL_TABLE_SIZE);
}

void
CirEnv__pushLocalScope(void)
{
    CirEnv__pushScope(TABLE_SIZE);
}

void
CirEnv__popScope(void)
{
    if (!scopeStackTop)
        cir_fatal("no more scopes to pop");
    scopeStackTop--;
    cir__xfree(scopes[scopeStackTop].names);
    cir__xfree(scopes[scopeStackTop].tags);
}

bool
CirEnv__isGlobal(void)
{
    return scopeStackTop <= 1;
}

int
CirEnv__findLocalName(CirName name, CirVarId *varId, CirTypedefId *typedefId, CirEnumItemId *enumItemId)
{
    for (uint32_t i = 0; i < scopeStackTop; i++) {
        const NameItem *nameItem = findNameItem(&scopes[scopeStackTop - i - 1], name);
        if (nameItem) {
            switch (nameItem->type) {
            case NAME_ITEM_VAR:
                *varId = nameItem->value.varId;
                return 1;
            case NAME_ITEM_TYPEDEF:
                *typedefId = nameItem->value.typedefId;
                return 2;
            case NAME_ITEM_ENUM:
                *enumItemId = nameItem->value.enumItemId;
                return 3;
            default:
                cir_bug("unreachable");
            }
        }
    }

    return 0;
}

int
CirEnv__findGlobalName(CirName name, CirVarId *varId, CirTypedefId *typedefId, CirEnumItemId *enumItemId)
{
    if (!scopeStackTop)
        cir_bug("No global scope present");

    const NameItem *nameItem = findNameItem(&scopes[0], name);
    if (!nameItem)
        return 0;

    switch (nameItem->type) {
    case NAME_ITEM_VAR:
        *varId = nameItem->value.varId;
        return 1;
    case NAME_ITEM_TYPEDEF:
        *typedefId = nameItem->value.typedefId;
        return 2;
    case NAME_ITEM_ENUM:
        *enumItemId = nameItem->value.enumItemId;
        return 3;
    default:
        cir_bug("unreachable");
    }
}

int
CirEnv__findCurrentScopeName(CirName name, CirVarId *varId, CirTypedefId *typedefId, CirEnumItemId *enumItemId)
{
    if (!scopeStackTop)
        cir_bug("No current scope present");

    const NameItem *nameItem = findNameItem(&scopes[scopeStackTop - 1], name);
    if (!nameItem)
        return 0;

    switch (nameItem->type) {
    case NAME_ITEM_VAR:
        *varId = nameItem->value.varId;
        return 1;
    case NAME_ITEM_TYPEDEF:
        *typedefId = nameItem->value.typedefId;
        return 2;
    case NAME_ITEM_ENUM:
        *enumItemId = nameItem->value.enumItemId;
        return 3;
    default:
        cir_bug("unreachable");
    }
}

void
CirEnv__setLocalNameAsVar(CirVarId vid)
{
    if (!scopeStackTop)
        cir_bug("No current scope present");

    CirName name = CirVar_getName(vid);
    if (!name)
        cir_bug("Var has no name!");

    NameItem nameItem;
    nameItem.key = name;
    nameItem.type = NAME_ITEM_VAR;
    nameItem.value.varId = vid;

    replaceNameItem(&scopes[scopeStackTop - 1], &nameItem);
}

void
CirEnv__setLocalNameAsTypedef(CirTypedefId tid)
{
    if (!scopeStackTop)
        cir_bug("No current scope present");

    CirName name = CirTypedef_getName(tid);
    if (!name)
        cir_bug("Typedef has no name!");

    NameItem nameItem;
    nameItem.key = name;
    nameItem.type = NAME_ITEM_TYPEDEF;
    nameItem.value.typedefId = tid;

    replaceNameItem(&scopes[scopeStackTop - 1], &nameItem);
}

void
CirEnv__setLocalNameAsEnumItem(CirEnumItemId enumItemId)
{
    if (!scopeStackTop)
        cir_bug("No current scope present");

    CirName name = CirEnumItem_getName(enumItemId);
    assert(name);

    NameItem nameItem;
    nameItem.key = name;
    nameItem.type = NAME_ITEM_ENUM;
    nameItem.value.enumItemId = enumItemId;

    replaceNameItem(&scopes[scopeStackTop - 1], &nameItem);
}

int
CirEnv__findLocalTag(CirName name, CirCompId *cid, CirEnumId *enumId)
{
    for (uint32_t i = 0; i < scopeStackTop; i++) {
        const TagItem *tagItem = findTagItem(&scopes[scopeStackTop - i - 1], name);
        if (!tagItem)
            continue;

        if (tagItem->isEnum) {
            *enumId = tagItem->u.enumId;
            return 2;
        } else {
            *cid = tagItem->u.compId;
            return 1;
        }
    }

    return 0;
}

void
CirEnv__setLocalTagAsComp(CirCompId cid)
{
    if (!scopeStackTop)
        cir_bug("no current scope present");

    CirName name = CirComp_getName(cid);
    if (!name)
        cir_bug("CirComp has no name!");

    TagItem tagItem;
    tagItem.key = name;
    tagItem.isEnum = false;
    tagItem.u.compId = cid;
    replaceTagItem(&scopes[scopeStackTop - 1], &tagItem);
}

void
CirEnv__setLocalTagAsEnum(CirEnumId enumId)
{
    if (!scopeStackTop)
        cir_bug("no current scope present");

    CirName name = CirEnum_getName(enumId);
    if (!name)
        cir_bug("CirEnum has no name!");

    TagItem tagItem;
    tagItem.key = name;
    tagItem.isEnum = true;
    tagItem.u.enumId = enumId;
    replaceTagItem(&scopes[scopeStackTop - 1], &tagItem);
}
