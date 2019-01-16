#include "cir_internal.h"

#define TABLE_SIZE 509

typedef struct NameItem {
    CirName key;
    bool isTypedef;
    union {
        CirVarId varId;
        CirTypedefId typedefId;
    } value;
    CirVarId varId;
} NameItem;

typedef struct TagItem {
    CirName key;
    CirCompId compId;
} TagItem;

typedef struct Scope {
    NameItem names[TABLE_SIZE];
    TagItem tags[TABLE_SIZE];
} Scope;

static Scope scopes[CIR_MAX_SCOPES];
static uint32_t scopeStackTop;

static const NameItem *
findNameItem(const Scope *scope, CirName name)
{
    for (uint32_t i = name % TABLE_SIZE; scope->names[i].key; i = (i + 1) % TABLE_SIZE) {
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
    for (i = item->key % TABLE_SIZE; scope->names[i].key && scope->names[i].key != item->key; i = (i + 1) % TABLE_SIZE);
    scope->names[i] = *item;
}

static const TagItem *
findTagItem(const Scope *scope, CirName name)
{
    for (uint32_t i = name % TABLE_SIZE; scope->tags[i].key; i = (i + 1) % TABLE_SIZE) {
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
    for (i = item->key % TABLE_SIZE; scope->tags[i].key && scope->tags[i].key != item->key; i = (i + 1) % TABLE_SIZE);
    scope->tags[i] = *item;
}

void
CirEnv__pushScope(void)
{
    if (scopeStackTop >= CIR_MAX_SCOPES)
        cir_fatal("too many nested scopes");

    memset(&scopes[scopeStackTop], 0, sizeof(Scope));
    scopeStackTop++;
}

void
CirEnv__popScope(void)
{
    if (!scopeStackTop)
        cir_fatal("no more scopes to pop");
    scopeStackTop--;
}

bool
CirEnv__isGlobal(void)
{
    return scopeStackTop <= 1;
}

int
CirEnv__findLocalName(CirName name, CirVarId *varId, CirTypedefId *typedefId)
{
    for (uint32_t i = 0; i < scopeStackTop; i++) {
        const NameItem *nameItem = findNameItem(&scopes[scopeStackTop - i - 1], name);
        if (nameItem) {
            if (nameItem->isTypedef) {
                *typedefId = nameItem->value.typedefId;
                return 2;
            } else {
                *varId = nameItem->value.varId;
                return 1;
            }
        }
    }

    return 0;
}

int
CirEnv__findGlobalName(CirName name, CirVarId *varId, CirTypedefId *typedefId)
{
    if (!scopeStackTop)
        cir_bug("No global scope present");

    const NameItem *nameItem = findNameItem(&scopes[0], name);
    if (!nameItem)
        return 0;

    if (nameItem->isTypedef) {
        *typedefId = nameItem->value.typedefId;
        return 2;
    } else {
        *varId = nameItem->value.varId;
        return 1;
    }
}

int
CirEnv__findCurrentScopeName(CirName name, CirVarId *varId, CirTypedefId *typedefId)
{
    if (!scopeStackTop)
        cir_bug("No current scope present");

    const NameItem *nameItem = findNameItem(&scopes[scopeStackTop - 1], name);
    if (!nameItem)
        return 0;

    if (nameItem->isTypedef) {
        *typedefId = nameItem->value.typedefId;
        return 2;
    } else {
        *varId = nameItem->value.varId;
        return 1;
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
    nameItem.isTypedef = false;
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
    nameItem.isTypedef = true;
    nameItem.value.typedefId = tid;

    replaceNameItem(&scopes[scopeStackTop - 1], &nameItem);
}

int
CirEnv__findLocalTag(CirName name, CirCompId *cid)
{
    for (uint32_t i = 0; i < scopeStackTop; i++) {
        const TagItem *tagItem = findTagItem(&scopes[scopeStackTop - i - 1], name);
        if (tagItem) {
            *cid = tagItem->compId;
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
    tagItem.compId = cid;
    replaceTagItem(&scopes[scopeStackTop - 1], &tagItem);
}
