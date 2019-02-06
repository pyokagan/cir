#include "cir_internal.h"

#define TABLE_SIZE 104729U
#define MAX_NAMES 806597U

struct Item {
    const char *key;
    uint32_t value;
};

static struct Item hashTable[TABLE_SIZE];

static const char *names[MAX_NAMES] = { "<name0>" };
static uint32_t numNames = 1;

static struct Item *
findItem(const char *name)
{
    for (size_t i = CirHash_str(name) % TABLE_SIZE; hashTable[i].key; i = (i + 1) % TABLE_SIZE) {
        if (!strcmp(hashTable[i].key, name)) {
            return &hashTable[i];
        }
    }
    return NULL;
}

static void
insertItem(const char *key, uint32_t value)
{
    size_t i;
    for (i = CirHash_str(key) % TABLE_SIZE; hashTable[i].key; i = (i + 1) % TABLE_SIZE);
    hashTable[i].key = key;
    hashTable[i].value = value;
}

CirName
CirName_of(const char *name)
{
    const struct Item *item;

    item = findItem(name);
    if (item) {
        return item->value;
    }

    if (numNames >= MAX_NAMES)
        cir_fatal("too many names");

    // Copy name
    size_t nameLen = strlen(name);
    char *nameMem = cir__balloc(nameLen + 1);
    memcpy(nameMem, name, nameLen + 1);
    uint32_t nameIdx = numNames++;
    names[nameIdx] = nameMem;
    insertItem(nameMem, nameIdx);
    return nameIdx;
}

const char *
CirName_cstr(CirName name)
{
    return names[name];
}

void
CirName_log(CirName name)
{
    CirLog_print(CirName_cstr(name));
}
