#include "cir_internal.h"
#include <assert.h>

// data1:
// bit 31: bitfield flag
// bits 30 to 0: bitfield size
#define data1ToBitfieldFlag(x) (((x) >> 31) & 0x01)
#define bitfieldFlagToData1(x) (((x) & 0x01) << 31)
#define data1ClearBitfieldFlag(x) ((x) & ~(0x01 << 31))
#define data1ToBitfieldSize(x) ((x) & 0x7fffffff)
#define bitfieldSizeToData1(x) ((x) & 0x7fffffff)
#define data1ClearBitfieldSize(x) ((x) & ~(0x7fffffff))
#define MAX_BITFIELD_SIZE 0x7fffffff

typedef struct CirFieldInfo {
    CirName name;
    uint32_t data1;
    const struct CirType *type;
} CirFieldInfo;

typedef CirArray(CirFieldInfo) CirFieldInfoArray;

// Reference-based struct, mutable
typedef struct CirComp {
    CirName name;
    bool isStruct;
    bool isDefined;
    CirFieldInfoArray fields;
} CirComp;

#define MAX_COMP 1024

static CirComp comps[MAX_COMP];
static uint32_t numComps = 1;

CirCompId
CirComp_new(void)
{
    if (numComps >= MAX_COMP)
        cir_bug("too many CirComps");

    return numComps++;
}

bool
CirComp_isStruct(CirCompId cid)
{
    assert(cid != 0);
    return comps[cid].isStruct;
}

void
CirComp_setStruct(CirCompId cid, bool val)
{
    assert(cid != 0);
    comps[cid].isStruct = val;
}

bool
CirComp_isDefined(CirCompId cid)
{
    assert(cid != 0);
    return comps[cid].isDefined;
}

void
CirComp_setDefined(CirCompId cid, bool defined)
{
    assert(cid != 0);
    comps[cid].isDefined = defined;
}

CirName
CirComp_getName(CirCompId cid)
{
    assert(cid != 0);
    return comps[cid].name;
}

void
CirComp_setName(CirCompId cid, CirName name)
{
    assert(cid != 0);
    comps[cid].name = name;
}

size_t
CirComp_getNumFields(CirCompId cid)
{
    assert(cid != 0);
    return comps[cid].fields.len;
}

void
CirComp_setNumFields(CirCompId cid, size_t newLen)
{
    assert(cid != 0);
    size_t oldLen = CirComp_getNumFields(cid);
    CirArray_alloc(&comps[cid].fields, newLen);
    // Zero off new mem
    for (size_t i = oldLen; i < newLen; i++) {
        comps[cid].fields.items[i].name = 0;
        comps[cid].fields.items[i].data1 = 0;
        comps[cid].fields.items[i].type = NULL;
    }
    comps[cid].fields.len = newLen;
}

CirName
CirComp_getFieldName(CirCompId cid, size_t idx)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    return comps[cid].fields.items[idx].name;
}

void
CirComp_setFieldName(CirCompId cid, size_t idx, CirName name)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    comps[cid].fields.items[idx].name = name;
}

const CirType *
CirComp_getFieldType(CirCompId cid, size_t idx)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    return comps[cid].fields.items[idx].type;
}

void
CirComp_setFieldType(CirCompId cid, size_t idx, const CirType *t)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    comps[cid].fields.items[idx].type = t;
}

bool
CirComp_hasFieldBitsize(CirCompId cid, size_t idx)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    return data1ToBitfieldFlag(comps[cid].fields.items[idx].data1);
}

size_t
CirComp_getFieldBitsize(CirCompId cid, size_t idx)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    return data1ToBitfieldSize(comps[cid].fields.items[idx].data1);
}

void
CirComp_setFieldBitsize(CirCompId cid, size_t idx, size_t bitsize)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    if (bitsize > MAX_BITFIELD_SIZE)
        cir_bug("bitfield size too large");
    comps[cid].fields.items[idx].data1 = bitfieldFlagToData1(1) | bitfieldSizeToData1(bitsize);
}

void
CirComp_clearFieldBitsize(CirCompId cid, size_t idx)
{
    assert(cid != 0);
    assert(idx < comps[cid].fields.len);
    comps[cid].fields.items[idx].data1 = 0;
}

bool
CirComp_getFieldByName(CirCompId comp_id, CirName name, size_t *out)
{
    assert(comp_id != 0);
    size_t numFields = comps[comp_id].fields.len;
    for (size_t i = 0; i < numFields; i++) {
        const CirFieldInfo *fieldInfo = &comps[comp_id].fields.items[i];
        if (fieldInfo->name == name) {
            *out = i;
            return true;
        }
    }
    return false;
}

// Rounds up `nrbits` to the nearest multiple of `roundto`.
// `roundto` must be a power of two.
static uint64_t
addTrailing(uint64_t nrbits, uint64_t roundto) {
    return (nrbits + roundto - 1) & (~(roundto - 1));
}

typedef struct OffsetAcc {
    // The first free bit
    uint64_t firstFree;
    // Where the previous field started
    uint64_t lastFieldStart;
    // The width of the previous field.
    // Might not be the same as firstFree - lastFieldStart because of internal padding.
    uint64_t lastFieldWidth;
} OffsetAcc;

static void
offsetOfFieldAccGcc(OffsetAcc *sofar, CirCompId cid, size_t field_idx, const CirMachine *mach)
{
    const CirType *fieldType = CirComp_getFieldType(cid, field_idx);
    fieldType = CirType_unroll(fieldType);
    uint64_t fieldTypeAlign = 8 * CirComp_getFieldAlign(cid, field_idx, mach);
    uint64_t fieldTypeBits = 8 * CirType_sizeof(fieldType, mach);

    if (!CirComp_hasFieldBitsize(cid, field_idx)) {
        // Align this field
        uint64_t newStart = addTrailing(sofar->firstFree, fieldTypeAlign);
        sofar->firstFree = newStart + fieldTypeBits;
        sofar->lastFieldStart = newStart;
        sofar->lastFieldWidth = fieldTypeBits;
        return;
    }

    size_t fieldBitsize = CirComp_getFieldBitsize(cid, field_idx);
    if (fieldBitsize == 0) {
        // A width of 0 means that we must end the current packing.
        // It seems that GCC pads only up to the alignment boundary for the type of this field.
        uint64_t firstFree = addTrailing(sofar->firstFree, fieldTypeAlign);
        sofar->firstFree = firstFree;
        sofar->lastFieldStart = firstFree;
        sofar->lastFieldWidth = 0;
        return;
    }

    // A bitfield cannot span more alignment boundaries of its type than the type itself.
    uint64_t numAlignmentBoundariesSpanned = (sofar->firstFree + fieldBitsize + fieldTypeAlign - 1) / fieldTypeAlign - sofar->firstFree / fieldTypeAlign;
    if (numAlignmentBoundariesSpanned > fieldTypeBits / fieldTypeAlign) {
        uint64_t start = addTrailing(sofar->firstFree, fieldTypeAlign);
        sofar->firstFree = start + fieldBitsize;
        sofar->lastFieldStart = start;
        sofar->lastFieldWidth = fieldBitsize;
    } else {
        // Just put the field down.
        uint64_t firstFree = sofar->firstFree;
        sofar->firstFree = firstFree + fieldBitsize;
        sofar->lastFieldStart = firstFree;
        sofar->lastFieldWidth = fieldBitsize;
    }
}

static void
offsetOfFieldAcc(OffsetAcc *sofar, CirCompId cid, size_t field_idx, const CirMachine *mach)
{
    if (mach->compiler == CIR_GCC)
        offsetOfFieldAccGcc(sofar, cid, field_idx, mach);
    else
        cir_bug("offsetOfFieldAcc: unsupported compiler");
}

// Alignment of a possibly-packed struct field, in bytes
uint64_t
CirComp_getFieldAlign(CirCompId cid, size_t field_idx, const CirMachine *mach)
{
    // TODO: handle packed field
    return CirType_alignof(CirComp_getFieldType(cid, field_idx), mach);
}

uint64_t
CirComp_getAlign(CirCompId cid, const CirMachine *mach)
{
    // For composite types the maximum alignment of any field inside
    if (mach->compiler == CIR_GCC) {
        uint64_t maxAlignSofar = 1;
        size_t numFields = CirComp_getNumFields(cid);
        for (size_t i = 0; i < numFields; i++) {
            if (CirComp_hasFieldBitsize(cid, i) && CirComp_getFieldBitsize(cid, i) == 0)
                continue; // On GCC, zero-width fields do not contribute to the alignment
            uint64_t alignment = CirComp_getFieldAlign(cid, i, mach);
            if (alignment > maxAlignSofar)
                maxAlignSofar = alignment;
        }
        return maxAlignSofar;
    } else {
        cir_bug("CirType_alignof: unsupported compiler");
    }
}

uint64_t
CirComp_getSize(CirCompId cid, const CirMachine *mach)
{
    size_t numFields = CirComp_getNumFields(cid);
    if (CirComp_isStruct(cid)) {
        // struct
        // Get the last offset
        OffsetAcc acc = {};
        for (size_t i = 0; i < numFields; i++)
            offsetOfFieldAcc(&acc, cid, i, mach);
        if (acc.firstFree == 0 && numFields != 0 && mach->compiler == CIR_MSVC) {
            // On MSVC if we have just zero-width bitfields then the length is 4 bytes and is not padded.
            return 4;
        }

        // Consider only the attributes on comp itself.
        uint64_t structAlign = 8 * CirComp_getAlign(cid, mach);
        return addTrailing(acc.firstFree, structAlign) / 8;
    } else {
        // union
        // Get the maximum of all fields
        uint64_t maxSize = 0;
        for (size_t i = 0; i < numFields; i++) {
            OffsetAcc acc = {};
            offsetOfFieldAcc(&acc, cid, i, mach);
            if (acc.firstFree > maxSize)
                maxSize = acc.firstFree;
        }
        // Add trailing by simulating adding an extra field
        return addTrailing(maxSize, 8 * CirComp_getAlign(cid, mach)) / 8;
    }
}

uint64_t
CirComp_getFieldBitsOffset(CirCompId comp_id, size_t field_idx, const CirMachine *mach)
{
    assert(comp_id != 0);
    size_t numFields = CirComp_getNumFields(comp_id);
    assert(field_idx < numFields);
    if (CirComp_isStruct(comp_id)) {
        // struct
        OffsetAcc acc = {};
        for (size_t i = 0; i <= field_idx; i++)
            offsetOfFieldAcc(&acc, comp_id, i, mach);
        return acc.lastFieldStart;
    } else {
        // union
        return 0;
    }
}

void
CirComp_log(CirCompId cid)
{
    if (cid == 0) {
        CirLog_print("<CirComp 0>");
        return;
    }

    CirLog_print(CirComp_isStruct(cid) ? "struct " : "union ");
    CirLog_printf("cid%u", (unsigned)cid);
    CirName name = CirComp_getName(cid);
    if (name) {
        CirLog_print("_");
        CirLog_print(CirName_cstr(name));
    }
}

// Isomorphic table
#define COMP_ISO_TABLE_SIZE 104729U

static uint64_t iso[COMP_ISO_TABLE_SIZE];
static bool isoDeleted[COMP_ISO_TABLE_SIZE];

bool
CirComp__isIsomorphic(CirCompId a, CirCompId b)
{
    assert(a != 0);
    assert(b != 0);

    if (a == b)
        return true;

    // a <= b
    if (a > b) {
        CirCompId tmp = a;
        a = b;
        b = tmp;
    }

    uint64_t key = ((uint64_t)a << 32) | b;
    for (size_t i = key % COMP_ISO_TABLE_SIZE; iso[i] || isoDeleted[i]; i = (i + 1) % COMP_ISO_TABLE_SIZE) {
        if (iso[i] == key)
            return true;
    }

    return false;
}

void
CirComp__markIsomorphic(CirCompId a, CirCompId b)
{
    assert(a != 0);
    assert(b != 0);

    if (a == b)
        return;

    // a <= b
    if (a > b) {
        CirCompId tmp = a;
        a = b;
        b = tmp;
    }

    if (CirComp__isIsomorphic(a, b))
        return;

    uint64_t key = ((uint64_t)a << 32) | b;
    size_t i;
    for (i = key % COMP_ISO_TABLE_SIZE; iso[i]; i = (i + 1) % COMP_ISO_TABLE_SIZE);
    iso[i] = key;
    isoDeleted[i] = false;
}

void
CirComp__unmarkIsomorphic(CirCompId a, CirCompId b)
{
    assert(a != 0);
    assert(b != 0);

    if (a == b)
        return;

    // a <= b
    if (a > b) {
        CirCompId tmp = a;
        a = b;
        b = tmp;
    }

    uint64_t key = ((uint64_t)a << 32) | b;
    for (size_t i = key % COMP_ISO_TABLE_SIZE; iso[i] || isoDeleted[i]; i = (i + 1) % COMP_ISO_TABLE_SIZE) {
        if (iso[i] == key) {
            iso[i] = 0;
            isoDeleted[i] = true;
            return;
        }
    }
}

size_t
CirComp_getNum(void)
{
    return numComps;
}
