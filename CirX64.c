#include "cir_internal.h"
#include <sys/mman.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#define GLOBAL_MEM_SIZE (1024 * 1024 * 1)

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8 8
#define REG_R9 9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14
#define REG_R15 15

// Cond
#define COND_B 0x02 // Jump if below (unsigned), CF = 1
#define COND_BE 0x06 // Jump if below or equal (unsigned), CF = 1 or ZF = 1
#define COND_L 0x0c // Jump if lesser (signed), SF != OF
#define COND_LE 0x0e // Jump if lesser or equal (signed), SF != OF or ZF = 1
#define COND_AE 0x03 // Jump if above or equal (unsigned), CF = 0
#define COND_A 0x07 // Jump if above (unsigned), CF = 0 and ZF = 0
#define COND_GE 0x0d // Jump if greater or equal (signed), SF = OF
#define COND_G 0x0f // Jump if greater (signed), ((ZF = 0) and SF = OF)
#define COND_E 0x04 // Jump if equal, ZF = 1
#define COND_NE 0x05 // Jump if not equal, ZF = 0
#define COND_O 0x00 // Jump if overflow, OF = 1

// Windows scratch registers: RAX, RCX, RDX, R8, R9, R10, R11

// Register used to store the start of our global memory pool
// This should maintain the same value throughout the function,
// otherwise we would need to movaps it in every time we wish to store a global var.
// NOTE: Important that this is not an argument register!
#define REG_GLOBAL_BASE REG_R10

// NOTE: Important that this is not an argument register!
#define REG_MEM_ADDR REG_R11

#define REG_OPERAND1 REG_RCX

#define REG_OPERAND2 REG_RDX

typedef struct VarInfo {
    enum {
        ALLOC_NONE, // not allocated yet
        ALLOC_STACK, // assigned a stack position, offset stored in offset
        ALLOC_GLOBAL, // assigned a position in our global memory pool, offset stored in offset
        ALLOC_EXTERNAL, // global location, stored in ptr
        ALLOC_COMPILING // still generating code for it
    } allocStatus;
    int32_t offset;
    void *ptr;
    size_t codeOffset;
} VarInfo;
static CirArray(VarInfo) varinfos;
static CirArray(CirVarId) compileQueue;
static CirArray(size_t) needPatch;
static CirArray(size_t) needStmtPatch;
static CirBBuf codebuf;
static CirArray(size_t) stmtLocs;

// NOTE: This memory MUST be zeroed by default!
static uint8_t globalMem[GLOBAL_MEM_SIZE] __attribute__((aligned(16)));
static size_t globalMemSize;

static void *currentPage; // Current memory page(s) we have mmaped
static size_t currentPageLen; // Number of bytes we have filled up
static size_t currentPageAlloc; // Total number of bytes we have allocated. If we don't have enough, we need to allocate a new page.

static const uint8_t callStubCode[] = {
    0x49, 0x89, 0xfa, // mov r10, rdi
    0x49, 0x8b, 0x7a, 0x08, // mov rdi, QWORD PTR [r10+0x8]
    0x49, 0x8b, 0x72, 0x10, // mov rsi, QWORD PTR [r10+0x10]
    0x49, 0x8b, 0x52, 0x18, // mov rdx, QWORD PTR [r10+0x18]
    0x49, 0x8b, 0x4a, 0x20, // mov rcx, QWORD PTR [r10+0x20]
    0x4d, 0x8b, 0x42, 0x28, // mov r8,QWORD PTR [r10+0x28]
    0x4d, 0x8b, 0x4a, 0x30, // mov r9,QWORD PTR [r10+0x30]
    0x4d, 0x8b, 0x12, // mov r10,QWORD PTR [r10]
    0x41, 0xff, 0xe2 // jmp r10
};
static uint64_t (*callStub)(void *mem);

static const char *reg64ToStr[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
static const char *reg32ToStr[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };

__attribute__((format(printf, 1, 2)))
static void
logInstr(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
#if 0
    vfprintf(stderr, fmt, va);
#endif
    va_end(va);
}

static VarInfo *
getVarInfo(CirVarId var_id)
{
    if (varinfos.len > var_id)
        return &varinfos.items[var_id];
    // We should have already allocated enough memory.
    // See the start of CirX64_call()
    assert(varinfos.alloc > var_id);
    for (size_t i = varinfos.len; i <= var_id; i++)
        varinfos.items[i].allocStatus = ALLOC_NONE;
    varinfos.len = var_id + 1;
    return &varinfos.items[var_id];
}

// Rounds up `nrbits` to the nearest multiple of `roundto`.
// `roundto` must be a power of two.
static uint64_t
addTrailing(uint64_t nrbits, uint64_t roundto) {
    uint64_t x = (nrbits + roundto - 1) & (~roundto + 1);
    assert(x % roundto == 0);
    return x;
}

static int32_t
globalMemAlloc(size_t n, size_t align)
{
    globalMemSize = addTrailing(globalMemSize, align);
    if (globalMemSize > INT_MAX)
        cir_fatal("out of global memory");
    int32_t offset = globalMemSize;
    globalMemSize += n;
    return offset;
}

// If space for var has not been allocated yet, resolve it or allocate it.
// After this, the allocStatus will never be ALLOC_NONE
static VarInfo *
resolveVar(CirVarId var_id)
{
    VarInfo *varinfo = getVarInfo(var_id);
    if (varinfo->allocStatus != ALLOC_NONE)
        return varinfo;

    const CirType *type = CirVar_getType(var_id);
    assert(type);

    if (CirType_isFun(type)) {
        // Does it have code?
        CirCodeId code_id = CirVar_getCode(var_id);
        if (code_id) {
            // Yes, it has. It's just not compiled yet. Mark it for compilation.
            CirArray_push(&compileQueue, &var_id);
            varinfo->allocStatus = ALLOC_COMPILING;
            varinfo->ptr = NULL;
            return varinfo;
        } else {
            // No, it does not.

            // If storage is static it MUST have a code.
            CirStorage storage = CirVar_getStorage(var_id);
            if (storage == CIR_STATIC) {
                CirLog_begin(CIRLOG_FATAL);
                CirLog_print("was called but there is no definition for: ");
                CirVar_logNameAndType(var_id);
                CirLog_end();
                exit(1);
            }

            CirName name = CirVar_getName(var_id);
            void *ptr;
            if (!CirDl_findSymbol(CirName_cstr(name), &ptr))
                cir_fatal("could not find symbol: %s", CirName_cstr(name));
            varinfo->allocStatus = ALLOC_EXTERNAL;
            varinfo->ptr = ptr;
            return varinfo;
        }
    } else {
        // If storage is static it MUST be allocated locally.
        CirStorage storage = CirVar_getStorage(var_id);
        if (storage != CIR_STATIC) {
            CirName name = CirVar_getName(var_id);
            void *ptr;
            if (CirDl_findSymbol(CirName_cstr(name), &ptr)) {
                varinfo->allocStatus = ALLOC_EXTERNAL;
                varinfo->ptr = ptr;
                return varinfo;
            }
            // If storage is EXTERN is must NOT be allocated locally
            if (storage == CIR_EXTERN) {
                cir_fatal("could not find symbol: %s", CirName_cstr(name));
            }
        }
        // Allocate memory
        size_t align = CirType_alignof(type, &CirMachine__build);
        size_t size = CirType_sizeof(type, &CirMachine__build);
        int32_t offset = globalMemAlloc(size, align);
        varinfo->allocStatus = ALLOC_GLOBAL;
        varinfo->offset = offset;
        return varinfo;
    }
}

// ======

static void
emitU8(uint8_t x)
{
    CirBBuf_grow(&codebuf, 1);
    codebuf.items[codebuf.len++] = x;
}

static void
emitI8(int8_t x)
{
    union { int8_t i8; uint8_t u8; } u = { .i8 = x };
    emitU8(u.u8);
}

static void
emitU32(uint32_t x)
{
    CirBBuf_grow(&codebuf, 4);
    size_t i = codebuf.len;
    codebuf.items[i++] = x & 0xff;
    codebuf.items[i++] = (x >> 8) & 0xff;
    codebuf.items[i++] = (x >> 16) & 0xff;
    codebuf.items[i++] = (x >> 24) & 0xff;
    codebuf.len = i;
}

static void
emitI32(int32_t x)
{
    union { int32_t i32; uint32_t u32; } u = { .i32 = x };
    emitU32(u.u32);
}

static void
emitU64(uint64_t x)
{
    CirBBuf_grow(&codebuf, 8);
    size_t i = codebuf.len;
    codebuf.items[i++] = x & 0xff;
    codebuf.items[i++] = (x >> 8) & 0xff;
    codebuf.items[i++] = (x >> 16) & 0xff;
    codebuf.items[i++] = (x >> 24) & 0xff;
    codebuf.items[i++] = (x >> 32) & 0xff;
    codebuf.items[i++] = (x >> 40) & 0xff;
    codebuf.items[i++] = (x >> 48) & 0xff;
    codebuf.items[i++] = (x >> 56) & 0xff;
    codebuf.len = i;
}

static void
emitREX(uint8_t W, uint8_t R, uint8_t X, uint8_t B)
{
    assert(W < 2); assert(R < 2); assert(X < 2); assert(B < 2);
    uint8_t val = 64 | (W << 3) | (R << 2) | (X << 1) | B;
    emitU8(val);
}

static void
emitModRM(uint8_t mod, uint8_t reg, uint8_t rm)
{
    assert(mod < 4); assert(reg < 8); assert(rm < 8);
    uint8_t val = (mod << 6) | (reg << 3) | rm;
    emitU8(val);
}

static void
emitSIB(uint8_t scale, uint8_t index, uint8_t base)
{
    assert(scale < 4); assert(index < 8); assert(base < 8);
    uint8_t val = (scale << 6) | (index << 3) | base;
    emitU8(val);
}

// emit LEA dstReg, [baseReg + indexReg * (2 ** scale)]
static void
emitLea(unsigned dstReg, unsigned baseReg, unsigned indexReg, unsigned scale)
{
    assert(dstReg < 16); assert(baseReg < 16); assert(indexReg < 16); assert(scale < 4);
    assert(baseReg != REG_RBP && baseReg != REG_R13); // We don't support this encoding yet
    assert(indexReg != REG_RSP); // We don't support this encoding yet
    emitREX(1, dstReg > 7, indexReg > 7, baseReg > 7);
    emitU8(0x8d);
    emitModRM(0, dstReg & 0x07, 4 /* use SIB byte */);
    emitSIB(scale, indexReg & 0x07, baseReg & 0x07);
}

// emit [srcReg64 + disp]
static void
emitMemDisp(unsigned dstReg64, unsigned srcReg64, int32_t disp)
{
    if (disp == 0 && srcReg64 != REG_RSP && srcReg64 != REG_RBP && srcReg64 != REG_R12 && srcReg64 != REG_R13) {
        emitModRM(0, dstReg64 & 0x07, srcReg64 & 0x07);
    } else if (disp <= SCHAR_MAX && disp >= SCHAR_MIN) {
        // [r/m + disp8]
        emitModRM(1, dstReg64 & 0x07, srcReg64 & 0x07);
        if (srcReg64 == REG_RSP || srcReg64 == REG_R12)
            emitSIB(0, 4, srcReg64 & 0x07);
        emitI8(disp);
    } else {
        // [r/m + disp32]
        emitModRM(2, dstReg64 & 0x07, srcReg64 & 0x07);
        if (srcReg64 == REG_RSP || srcReg64 == REG_R12)
            emitSIB(0, 4, srcReg64 & 0x07);
        emitI32(disp);
    }
}

// 64-bit add
static void
emitAdd64(unsigned dstReg64, unsigned srcReg64)
{
    assert(dstReg64 < 16); assert(srcReg64 < 16);
    logInstr("add\t%s, %s\n", reg64ToStr[dstReg64], reg64ToStr[dstReg64]);
    emitREX(1, srcReg64 > 7, 0, dstReg64 > 7);
    emitU8(0x01);
    emitModRM(3, srcReg64 & 0x07, dstReg64 & 0x07);
}

static void
emitAddImmI32(unsigned dstReg64, int32_t imm)
{
    assert(dstReg64 < 16);
    logInstr("add\t%s, %d\n", reg64ToStr[dstReg64], imm);
    emitREX(1, 0, 0, dstReg64 > 7);
    emitU8(0x81);
    emitModRM(3, 0, dstReg64 & 0x07);
    emitU32(imm);
}

static void
emitSub64(unsigned dstReg64, unsigned srcReg64)
{
    assert(dstReg64 < 16); assert(srcReg64 < 16);
    logInstr("sub\t%s, %s\n", reg64ToStr[dstReg64], reg64ToStr[srcReg64]);
    emitREX(1, dstReg64 > 7, 0, srcReg64 > 7);
    emitU8(0x2B);
    emitModRM(3, dstReg64 & 0x07, srcReg64 & 0x07);
}

static void
emitMulImm32(unsigned dstReg, unsigned srcReg, int32_t imm)
{
    assert(dstReg < 16); assert(srcReg < 16);
    bool fitsInOneByte = imm >= SCHAR_MIN && imm <= SCHAR_MAX;
    emitREX(1, dstReg > 7, 0, srcReg > 7);
    emitU8(fitsInOneByte ? 0x6b : 0x69);
    emitModRM(3, dstReg & 0x07, srcReg & 0x07);
    if (fitsInOneByte)
        emitI8(imm);
    else
        emitI32(imm);
}

// Move immediate value into register
static void
emitMovImmU64(unsigned reg64, uint64_t value)
{
    assert(reg64 < 16);
    logInstr("movabs\t%s, 0x%lx\n", reg64ToStr[reg64], value);
    emitREX(1, 0, 0, reg64 > 7);
    emitU8(0xB8 + (reg64 & 0x07)); // 0xB8 + op1_reg
    emitU64(value); // op2_imm64
}

// Move immediate value into register
static void
emitMovImmI64(unsigned reg64, int64_t value)
{
    union { uint64_t u64; int64_t i64; } u = { .i64 = value };
    emitMovImmU64(reg64, u.u64);
}

static void
emitMovImmPtr(unsigned reg, const void *ptr)
{
    union { uint64_t u64; const void *ptr; } u = { .ptr = ptr };
    emitMovImmU64(reg, u.u64);
}

static void
emitMovReg3264(unsigned dstReg64, unsigned srcReg64, bool wide)
{
    assert(dstReg64 < 16); assert(srcReg64 < 16);
    if (wide || dstReg64 > 7 || srcReg64 > 7)
        emitREX(1, dstReg64 > 7, 0, srcReg64 > 7);
    emitU8(0x8B);
    emitModRM(3, dstReg64 & 0x07, srcReg64 & 0x07);
}

static void
emitMovReg32(unsigned dstReg32, unsigned srcReg32)
{
    logInstr("mov\t%s, %s\n", reg32ToStr[dstReg32], reg32ToStr[dstReg32]);
    emitMovReg3264(dstReg32, srcReg32, false);
}

static void
emitMovReg64(unsigned dstReg64, unsigned srcReg64)
{
    logInstr("mov\t%s, %s\n", reg64ToStr[dstReg64], reg64ToStr[dstReg64]);
    emitMovReg3264(dstReg64, srcReg64, true);
}

// Load memory into register
static void
emitLoad3264(unsigned dstReg64, unsigned srcReg64, int32_t disp, bool wide, bool _signed)
{
    assert(dstReg64 < 16); assert(srcReg64 < 16);
    if (wide || dstReg64 > 7 || srcReg64 > 7)
        emitREX(wide, dstReg64 > 7, 0, srcReg64 > 7);
    emitU8(_signed ? 0x63 : 0x8B);
    emitMemDisp(dstReg64, srcReg64, disp);
}

static void
emitLoad8(unsigned dstReg, unsigned srcReg, int32_t disp, bool _signed)
{
    assert(dstReg < 16); assert(srcReg < 16);
    if (_signed || dstReg > 7 || srcReg > 7)
        emitREX(_signed, dstReg > 7, 0, srcReg > 7);
    if (_signed) {
        emitU8(0x0f);
        emitU8(0xbe);
    } else {
        emitU8(0x8a);
    }
    emitMemDisp(dstReg, srcReg, disp);
}

static void
emitLoad16(unsigned dstReg, unsigned srcReg, int32_t disp, bool _signed)
{
    assert(dstReg < 16); assert(srcReg < 16);
    if (!_signed)
        emitU8(0x66); // size override
    if (_signed || dstReg > 7 || srcReg > 7)
        emitREX(_signed, dstReg > 7, 0, srcReg > 7);
    if (_signed) {
        emitU8(0x0f);
        emitU8(0xbf);
    } else {
        emitU8(0x8b);
    }
    emitMemDisp(dstReg, srcReg, disp);
}

static void
emitLoad64(unsigned dstReg64, unsigned srcReg64, int32_t disp)
{
    emitLoad3264(dstReg64, srcReg64, disp, true, false);
}

static void
emitLoad32(unsigned dstReg32, unsigned srcReg32, int32_t disp, bool _signed)
{
    emitLoad3264(dstReg32, srcReg32, disp, _signed, _signed);
}

static void
emitLoadIkind(uint32_t ikind, unsigned dstReg, unsigned memReg, int32_t disp)
{
    switch (ikind) {
    case CIR_ICHAR:
        if (CirMachine__build.charIsUnsigned)
            emitLoadIkind(CIR_IUCHAR, dstReg, memReg, disp);
        else
            emitLoadIkind(CIR_ISCHAR, dstReg, memReg, disp);
        break;
    case CIR_ISCHAR:
    case CIR_IUCHAR:
    case CIR_IBOOL:
        assert(sizeof(char) == 1);
        assert(sizeof(bool) == 1);
        emitLoad8(dstReg, memReg, disp, ikind == CIR_ISCHAR);
        break;
    case CIR_ISHORT:
    case CIR_IUSHORT:
        assert(sizeof(short) == 2);
        emitLoad16(dstReg, memReg, disp, ikind == CIR_ISHORT);
        break;
    case CIR_IINT:
    case CIR_IUINT:
        assert(sizeof(int) == 4);
        emitLoad32(dstReg, memReg, disp, ikind == CIR_IINT);
        break;
    case CIR_ILONG:
    case CIR_IULONG:
    case CIR_ILONGLONG:
    case CIR_IULONGLONG:
        assert(sizeof(long) == 8);
        assert(sizeof(long long) == 8);
        emitLoad64(dstReg, memReg, disp);
        break;
    default:
        cir_bug("unsupported ikind");
    }
}

static void
emitLoad(unsigned dstReg, const CirValue *value)
{
    if (CirValue_isInt(value)) {
        // TODO: maybe use appropriate encoding based on imm
        emitMovImmU64(dstReg, CirValue_getU64(value));
        return;
    }

    if (CirValue_isString(value)) {
        emitMovImmPtr(dstReg, CirValue_getString(value));
        return;
    }

    assert(CirValue_isLval(value));

    bool deref = CirValue_isMem(value);
    CirVarId var_id = CirValue_getVar(value);
    const VarInfo *varinfo = resolveVar(var_id);
    uint64_t bitsOffset = 0;
    const CirType *type = CirValue_computeTypeAndBitsOffset(value, &bitsOffset, &CirMachine__build);
    assert(type);
    type = CirType_unroll(type);
    bitsOffset /= 8;
    if (bitsOffset > INT_MAX)
        cir_fatal("field offset too large");
    int32_t fieldOffset = bitsOffset;

    uint32_t ikind = CirType_isInt(type);
    if (CirType_isPtr(type))
        ikind = CIR_IULONG;

    if (deref) {
        // First we need to load the address from var
        emitLoad(dstReg, CirValue_ofVar(var_id));

        // Then we load the value
        if (ikind)
            emitLoadIkind(ikind, dstReg, dstReg, fieldOffset);
        else if (CirType_isArray(type))
            emitAddImmI32(dstReg, fieldOffset);
        else
            cir_fatal("emitLoad called on non-int/ptr/array var");
    } else if (varinfo->allocStatus == ALLOC_STACK) {
        if (ikind) {
            emitLoadIkind(ikind, dstReg, REG_RSP, varinfo->offset);
        } else if (CirType_isArray(type)) {
            // TODO: Probably can optimize into a single instruction
            emitMovReg64(dstReg, REG_RSP);
            emitAddImmI32(dstReg, varinfo->offset);
        } else {
            cir_fatal("emitLoad called on non-int/ptr/array var");
        }
    } else if (varinfo->allocStatus == ALLOC_GLOBAL) {
        if (ikind) {
            emitLoadIkind(ikind, dstReg, REG_GLOBAL_BASE, varinfo->offset);
        } else if (CirType_isArray(type)) {
            // TODO: Probably can optimize into a single instruction
            emitMovReg64(dstReg, REG_GLOBAL_BASE);
            emitAddImmI32(dstReg, varinfo->offset);
        } else {
            cir_fatal("emitLoad called on non-int/ptr/array var");
        }
    } else {
        assert(varinfo->allocStatus == ALLOC_EXTERNAL);
        uint64_t ptr = (uint64_t)varinfo->ptr;
        ptr += fieldOffset;
        if (ikind) {
            emitMovImmU64(dstReg, ptr);
            emitLoadIkind(ikind, dstReg, dstReg, 0);
        } else if (CirType_isArray(type)) {
            emitMovImmU64(dstReg, ptr);
        } else {
            cir_fatal("emitLoad called on non-int/ptr/array var");
        }
    }
}

static void
emitLoadVarAddress(unsigned dstReg, CirVarId var_id, int32_t disp)
{
    const VarInfo *varinfo = resolveVar(var_id);
    if (varinfo->allocStatus == ALLOC_GLOBAL) {
        emitMovReg64(dstReg, REG_GLOBAL_BASE);
        emitAddImmI32(dstReg, varinfo->offset + disp);
    } else if (varinfo->allocStatus == ALLOC_STACK) {
        emitMovReg64(dstReg, REG_RSP);
        emitAddImmI32(dstReg, varinfo->offset + disp);
    } else if (varinfo->allocStatus == ALLOC_EXTERNAL) {
        uint64_t ptr = (uint64_t)varinfo->ptr;
        emitMovImmU64(dstReg, ptr + disp);
    } else if (varinfo->allocStatus == ALLOC_COMPILING) {
        // Need to backpatch later
        // emitMovImmU64 with placeholder constant
        assert(!disp);
        assert(dstReg < 16);
        emitREX(1, 0, 0, dstReg > 7);
        emitU8(0xB8 + (dstReg & 0x07));
        CirArray_push(&needPatch, &codebuf.len);
        emitU64(var_id);
    } else {
        cir_bug("boo");
    }
}

static void
emitLoadAddress(unsigned dstReg, const CirValue *value)
{
    uint32_t ikind = CirValue_isInt(value);
    if (ikind)
        cir_fatal("cannot get address of an integer constant");

    if (CirValue_isString(value)) {
        emitMovImmPtr(dstReg, CirValue_getString(value));
        return;
    }

    assert(CirValue_isLval(value));

    bool deref = CirValue_isMem(value);
    CirVarId var_id = CirValue_getVar(value);
    uint64_t bitsOffset = CirValue_computeBitsOffset(value, &CirMachine__build);
    bitsOffset /= 8;
    if (bitsOffset > INT_MAX)
        cir_fatal("field offset too large");
    int32_t fieldOffset = bitsOffset;

    if (deref) {
        // First we need to load the address from var
        emitLoad(dstReg, CirValue_ofVar(var_id));

        // Then, we add the fieldOffset to the value
        emitAddImmI32(dstReg, fieldOffset);
    } else {
        emitLoadVarAddress(dstReg, var_id, fieldOffset);
    }
}

// Store register into memory
static void
emitStore163264(unsigned memReg64, int32_t disp, unsigned srcReg64, bool wide, bool size_override)
{
    assert(memReg64 < 16); assert(srcReg64 < 16);
    if (size_override)
        emitU8(0x66);
    if (wide || memReg64 > 7 || srcReg64 > 7)
        emitREX(wide, srcReg64 > 7, 0, memReg64 > 7);
    emitU8(0x89);
    emitMemDisp(srcReg64, memReg64, disp);
}

static void
emitStore8(unsigned memReg, int32_t disp, unsigned srcReg)
{
    assert(memReg < 16); assert(srcReg < 16);
    if (memReg > 7 || srcReg > 7)
        emitREX(0, srcReg > 7, 0, memReg > 7);
    emitU8(0x88);
    emitMemDisp(srcReg, memReg, disp);
}

static void
emitStore16(unsigned memReg64, int32_t disp, unsigned srcReg64)
{
    emitStore163264(memReg64, disp, srcReg64, false, true);
}

static void
emitStore32(unsigned memReg64, int32_t disp, unsigned srcReg64)
{
    emitStore163264(memReg64, disp, srcReg64, false, false);
}

static void
emitStore64(unsigned memReg64, int32_t disp, unsigned srcReg64)
{
    emitStore163264(memReg64, disp, srcReg64, true, false);
}

static void
emitStoreIkind(uint32_t ikind, unsigned memReg, int32_t disp, unsigned srcReg)
{
    switch (ikind) {
    case CIR_ICHAR:
    case CIR_ISCHAR:
    case CIR_IUCHAR:
    case CIR_IBOOL:
        assert(sizeof(char) == 1);
        assert(sizeof(bool) == 1);
        emitStore8(memReg, disp, srcReg);
        return;
    case CIR_ISHORT:
    case CIR_IUSHORT:
        assert(sizeof(short) == 2);
        emitStore16(memReg, disp, srcReg);
        return;
    case CIR_IINT:
    case CIR_IUINT:
        assert(sizeof(int) == 4);
        emitStore32(memReg, disp, srcReg);
        return;
    case CIR_ILONG:
    case CIR_IULONG:
    case CIR_ILONGLONG:
    case CIR_IULONGLONG:
        assert(sizeof(long) == 8);
        assert(sizeof(long long) == 8);
        emitStore64(memReg, disp, srcReg);
        return;
    default:
        cir_bug("unsupported ikind");
    }
}

static void
emitStore(const CirValue *value, unsigned srcReg)
{
    if (!CirValue_isLval(value))
        cir_bug("emitStore called on non-lval");
    bool deref = CirValue_isMem(value);
    CirVarId var_id = CirValue_getVar(value);
    const VarInfo *varinfo = resolveVar(var_id);
    uint64_t bitsOffset = 0;
    const CirType *type = CirValue_computeTypeAndBitsOffset(value, &bitsOffset, &CirMachine__build);
    assert(type);
    type = CirType_unroll(type);
    bitsOffset /= 8;
    if (bitsOffset > INT_MAX)
        cir_fatal("emitStore: offset too large");
    int32_t fieldOffset = bitsOffset;

    uint32_t ikind = CirType_isInt(type);
    if (CirType_isPtr(type))
        ikind = CIR_IULONG;
    if (!ikind) {
        CirLog_begin(CIRLOG_BUG);
        CirLog_print("emitStore called on non-int/ptr var: ");
        CirVar_logNameAndType(var_id);
        CirLog_end();
        abort();
    }

    if (deref) {
        // First we need to load the address from var into REG_MEM_ADDR
        emitLoad(REG_MEM_ADDR, CirValue_ofVar(var_id));

        // Then we store the value
        emitStoreIkind(ikind, REG_MEM_ADDR, fieldOffset, srcReg);
    } else {
        if (varinfo->allocStatus == ALLOC_STACK) {
            emitStoreIkind(ikind, REG_RSP, varinfo->offset, srcReg);
        } else if (varinfo->allocStatus == ALLOC_GLOBAL) {
            emitStoreIkind(ikind, REG_GLOBAL_BASE, varinfo->offset, srcReg);
        } else {
            assert(varinfo->allocStatus == ALLOC_EXTERNAL);
            uint64_t ptr = (uint64_t)varinfo->ptr;
            ptr += fieldOffset;
            emitMovImmU64(REG_MEM_ADDR, ptr);
            emitStoreIkind(ikind, REG_MEM_ADDR, 0, srcReg);
        }
    }
}

static void
emitCall(unsigned reg)
{
    if (reg > 7)
        emitREX(0, 0, 0, reg > 7);
    emitU8(0xFF);
    emitModRM(3, 2, reg & 0x07);
}

static void
emitRet(void)
{
    emitU8(0xc3);
}

static void
emitJumpToStmt(CirStmtId stmt_id)
{
    emitU8(0xE9);
    assert(sizeof(stmt_id) == 4);
    CirArray_push(&needStmtPatch, &codebuf.len);
    emitU32(stmt_id);
}

static void
emitCondJumpToStmt(uint8_t cond, CirStmtId stmt_id)
{
    assert(cond < 16);
    emitU8(0x0f);
    emitU8(0x80 + cond);
    assert(sizeof(stmt_id) == 4);
    CirArray_push(&needStmtPatch, &codebuf.len);
    emitU32(stmt_id);
}

static void
emitCmp(unsigned reg1, unsigned reg2)
{
    assert(reg1 < 16); assert(reg2 < 16);
    emitREX(1, reg1 > 7, 0, reg2 > 7);
    emitU8(0x3b);
    emitModRM(3, reg1 & 0x07, reg2 & 0x07);
}

// =====

static void
emitAddPtrInt(const CirValue *dst, const CirValue *ptrValue, const CirType *ptrType, const CirValue *intValue, const CirType *intType)
{
    static unsigned sizeToFactor[] = { [1] = 0, [2] = 1, [4] = 2, [8] = 3 };
    // Code sequence depends on whether the size of each element is in {1, 2, 4, 8}.
    const CirType *baseType = CirType_getBaseType(ptrType);
    uint64_t baseTypeSize = CirType_sizeof(baseType, &CirMachine__build);
    if (CirValue_isInt(intValue)) {
        // Can use ADD + pre-computed offset
        uint32_t ikind = CirType_isInt(CirType_unroll(intType));
        assert(ikind);
        emitLoad(REG_OPERAND1, ptrValue);
        int32_t imm;
        if (CirIkind_isSigned(ikind, &CirMachine__build)) {
            int64_t val = CirValue_getI64(intValue) * baseTypeSize;
            assert(val >= INT_MIN && val <= INT_MAX);
            imm = val;
        } else {
            uint64_t val = CirValue_getU64(intValue) * baseTypeSize;
            assert(val <= INT_MAX);
            imm = val;
        }
        emitAddImmI32(REG_OPERAND1, imm);
        emitStore(dst, REG_OPERAND1);
    } else if (baseTypeSize == 1) {
        // Can use ADD
        emitLoad(REG_OPERAND1, ptrValue);
        emitLoad(REG_OPERAND2, intValue);
        emitAdd64(REG_OPERAND1, REG_OPERAND2);
        emitStore(dst, REG_OPERAND1);
    } else if (baseTypeSize == 2 || baseTypeSize == 4 || baseTypeSize == 8) {
        // Can use LEA
        emitLoad(REG_OPERAND1, ptrValue);
        emitLoad(REG_OPERAND2, intValue);
        emitLea(REG_OPERAND1, REG_OPERAND1, REG_OPERAND2, sizeToFactor[baseTypeSize]);
        emitStore(dst, REG_OPERAND1);
    } else {
        // Compute offset manually
        assert(baseTypeSize <= INT_MAX);
        emitLoad(REG_OPERAND1, ptrValue);
        emitLoad(REG_OPERAND2, intValue);
        emitMulImm32(REG_OPERAND2, REG_OPERAND2, baseTypeSize);
        emitAdd64(REG_OPERAND1, REG_OPERAND2);
        emitStore(dst, REG_OPERAND1);
    }
}

static void
emitBinop(CirStmtId stmt_id)
{
    assert(CirStmt_isBinOp(stmt_id));
    uint32_t op = CirStmt_getOp(stmt_id);
    const CirValue *dst = CirStmt_getDst(stmt_id);
    const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
    const CirValue *operand2 = CirStmt_getOperand2(stmt_id);
    if (!operand1)
        cir_fatal("emitBinop: operand1 has no value");
    if (!operand2)
        cir_fatal("emitBinop: operand2 has no value");
    const CirType *operand1Type = CirValue_getType(operand1);
    if (!operand1Type)
        cir_fatal("emitBinop: operand1 has no type");
    const CirType *operand2Type = CirValue_getType(operand2);
    if (!operand2Type)
        cir_fatal("emitBinop: operand2 has no type");
    operand1Type = CirType_lvalConv(operand1Type);
    operand2Type = CirType_lvalConv(operand2Type);
    // Perform operation
    switch (op) {
    case CIR_BINOP_PLUS: {
        // Depends on the type of the operands
        const CirType *operand1UnrolledType = CirType_unroll(operand1Type);
        const CirType *operand2UnrolledType = CirType_unroll(operand2Type);
        uint32_t ikind;
        if (CirType_isArithmetic(operand1UnrolledType) && CirType_isArithmetic(operand2UnrolledType)) {
            // TODO: support floats here
            emitLoad(REG_OPERAND1, operand1);
            emitLoad(REG_OPERAND2, operand2);
            emitAdd64(REG_OPERAND1, REG_OPERAND2);
            emitStore(dst, REG_OPERAND1);
        } else if (CirType_isPtr(operand1UnrolledType) && (ikind = CirType_isInt(operand2UnrolledType))) {
            // ptr + int
            emitAddPtrInt(dst, operand1, operand1UnrolledType, operand2, operand2UnrolledType);
        } else if ((ikind = CirType_isInt(operand1UnrolledType)) && CirType_isPtr(operand2UnrolledType)) {
            // int + ptr
            emitAddPtrInt(dst, operand2, operand2Type, operand1, operand1Type);
        } else {
            cir_fatal("CIR_BINOP_PLUS: invalid operand types");
        }
        break;
    }
    default:
        cir_bug("TODO: binop");
    }
}

static void
doCompile(CirVarId var_id)
{
#if 0
    CirLog_begin(CIRLOG_DEBUG);
    CirCode_dump(CirVar_getCode(var_id));
    CirLog_end();
#endif

    // var should be a function
    const CirType *type = CirVar_getType(var_id);
    assert(type);
    assert(CirType_isFun(type));
    size_t numArgs = CirType_getNumParams(type);

    VarInfo *varinfo = getVarInfo(var_id);
    assert(varinfo->allocStatus == ALLOC_COMPILING);

    CirCodeId code_id = CirVar_getCode(var_id);
    assert(code_id);

    // TODO: Determine how much space we need to reserve for calling other functions

    // Allocate memory for stack variables
    size_t stackSize = 0;
    size_t numLocals = CirCode_getNumVars(code_id);
    for (size_t i = 0; i < numLocals; i++) {
        CirVarId local_id = CirCode_getVar(code_id, i);
        assert(CirVar_getOwner(local_id) == code_id);
        VarInfo *localinfo = getVarInfo(local_id);
        assert(localinfo->allocStatus == ALLOC_NONE || localinfo->allocStatus == ALLOC_COMPILING);
        if (localinfo->allocStatus == ALLOC_NONE) {
            const CirType *varType = CirVar_getType(local_id);
            size_t sizeofVar = CirType_sizeof(varType, &CirMachine__build);
            size_t alignofVar = CirType_alignof(varType, &CirMachine__build);
            stackSize = addTrailing(stackSize, alignofVar);
            localinfo->allocStatus = ALLOC_STACK;
            // Plus 8 is for the padding we are going to add
            localinfo->offset = (int32_t)(stackSize + 8);
            stackSize += sizeofVar;
        }
    }

    stackSize = addTrailing(stackSize, 16);
    stackSize += 8; // Add an additional 8 bytes.
    // If we call other functions CALL will push the 8 bytes return pointer, rounding it up to 16 bytes.

    // Save our starting code offset
    varinfo->codeOffset = codebuf.len;

    // Prolog: allocate stack space
    emitAddImmI32(REG_RSP, -((int32_t)stackSize));

    // Prolog: Copy arguments from registers to memory (lol)
    static const unsigned idxToReg[6] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
    for (size_t i = 0; i < numArgs && i < 6; i++) {
        CirVarId arg_vid = CirVar_getFormal(var_id, i);
        const CirType *argType = CirVar_getType(arg_vid);
        assert(argType);
        VarInfo *argInfo = getVarInfo(arg_vid);
        assert(argInfo->allocStatus == ALLOC_STACK);
        emitStore(CirValue_ofVar(arg_vid), idxToReg[i]);
    }

    // Prolog: load global mem address
    emitMovImmPtr(REG_GLOBAL_BASE, globalMem);

    // Generate code!
    CirStmtId stmt_id = CirCode_getFirstStmt(code_id);
    while (stmt_id) {
        // Save address of stmt for computing relative jumps later
        assert(stmt_id < stmtLocs.len);
        stmtLocs.items[stmt_id] = codebuf.len;

        if (CirStmt_isNop(stmt_id)) {
            // Do nothing... ???
        } else if (CirStmt_isUnOp(stmt_id)) {
            uint32_t op = CirStmt_getOp(stmt_id);
            const CirValue *dst = CirStmt_getDst(stmt_id);
            const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
            // Some unops have to be handled specially as their operands may not fit
            // in a register.
            switch (op) {
            case CIR_UNOP_IDENTITY: {
                const CirType *dstType = CirValue_getType(dst);
                const CirType *operand1Type = CirValue_getType(operand1);
                uint64_t dstType_size = CirType_sizeof(dstType, &CirMachine__build);
                uint64_t operand1Type_size = CirType_sizeof(operand1Type, &CirMachine__build);
                if (dstType_size <= 8 && operand1Type_size <= 8) {
                    // Both fit in registers, can do register copy
                    emitLoad(REG_OPERAND1, operand1);
                    emitStore(dst, REG_OPERAND1);
                } else {
                    // Do a memcpy
                    if (dstType_size != operand1Type_size)
                        cir_fatal("simple assign: size mismatch");
                    emitLoadAddress(REG_RDI, dst); // dst pointer
                    emitLoadAddress(REG_RSI, operand1); // src pointer
                    emitMovImmU64(REG_RDX, dstType_size); // len
                    emitMovImmPtr(REG_RAX, memmove); // target
                    emitCall(REG_RAX);
                    // Restore global mem address since it might have been clobbered
                    emitMovImmPtr(REG_GLOBAL_BASE, globalMem);
                }
                break;
            }
            default:
                cir_bug("TODO: unop");
            }
        } else if (CirStmt_isBinOp(stmt_id)) {
            emitBinop(stmt_id);
        } else if (CirStmt_isCall(stmt_id)) {
            const CirValue *dst = CirStmt_getDst(stmt_id);
            const CirValue *target = CirStmt_getOperand1(stmt_id);
            size_t numArgs = CirStmt_getNumArgs(stmt_id);
            // Load args into their respective registers
            assert(numArgs <= 6);
            for (size_t i = 0; i < numArgs; i++) {
                emitLoad(idxToReg[i], CirStmt_getArg(stmt_id, i));
            }

            // Call function
            const CirType *targetType = CirValue_getType(target);
            if (CirValue_isVar(target) && CirType_isFun(targetType) && CirValue_getNumFields(target) == 0) {
                CirVarId vid = CirValue_getVar(target);
                emitLoadVarAddress(REG_RAX, vid, 0);
            } else if (CirType_isPtr(targetType)) {
                emitLoad(REG_RAX, target);
            } else {
                cir_bug("type not callable");
            }
            emitCall(REG_RAX);

            // Restore global mem address since it might have been clobbered
            emitMovImmPtr(REG_GLOBAL_BASE, globalMem);

            // Save dst
            if (dst)
                emitStore(dst, REG_RAX);
        } else if (CirStmt_isReturn(stmt_id)) {
            const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
            if (operand1) {
                emitLoad(REG_RAX, operand1);
            }
            // Epilog: Deallocate stack space
            emitAddImmI32(REG_RSP, (int32_t)stackSize);
            // Epilog: return
            emitRet();
        } else if (CirStmt_isCmp(stmt_id)) {
            uint32_t condop = CirStmt_getOp(stmt_id);
            const CirValue *operand1 = CirStmt_getOperand1(stmt_id);
            const CirValue *operand2 = CirStmt_getOperand2(stmt_id);
            CirStmtId jumpTarget = CirStmt_getJumpTarget(stmt_id);
            assert(jumpTarget);
            emitLoad(REG_OPERAND1, operand1);
            emitLoad(REG_OPERAND2, operand2);
            emitCmp(REG_OPERAND1, REG_OPERAND2);
            // These condops require us to know if the comparison is signed or not
            const CirType *operand1Type, *operand2Type, *convType;
            bool isSigned = false;
            switch (condop) {
            case CIR_CONDOP_LT:
            case CIR_CONDOP_GT:
            case CIR_CONDOP_LE:
            case CIR_CONDOP_GE:
                operand1Type = CirValue_getType(operand1);
                operand2Type = CirValue_getType(operand2);
                convType = CirType__arithmeticConversion(operand1Type, operand2Type, &CirMachine__build);
                uint32_t ikind = CirType_isInt(convType);
                assert(ikind);
                isSigned = CirIkind_isSigned(ikind, &CirMachine__build);
                break;
            default:
                break;
            }
            // Emit appropriate jump
            switch (condop) {
            case CIR_CONDOP_LT:
                emitCondJumpToStmt(isSigned ? COND_L : COND_B, jumpTarget);
                break;
            case CIR_CONDOP_GT:
                emitCondJumpToStmt(isSigned ? COND_G : COND_A, jumpTarget);
                break;
            case CIR_CONDOP_LE:
                emitCondJumpToStmt(isSigned ? COND_LE : COND_BE, jumpTarget);
                break;
            case CIR_CONDOP_GE:
                emitCondJumpToStmt(isSigned ? COND_GE : COND_AE, jumpTarget);
                break;
            case CIR_CONDOP_EQ:
                emitCondJumpToStmt(COND_E, jumpTarget);
                break;
            case CIR_CONDOP_NE:
                emitCondJumpToStmt(COND_NE, jumpTarget);
                break;
            default:
                cir_bug("condop");
            }
        } else if (CirStmt_isGoto(stmt_id)) {
            CirStmtId jumpTarget = CirStmt_getJumpTarget(stmt_id);
            assert(jumpTarget);
            emitJumpToStmt(jumpTarget);
        } else {
            cir_bug("CirX64: stmt kind not implemented");
        }
        stmt_id = CirStmt_getNext(stmt_id);
    }

    // Epilog: Deallocate stack space
    emitAddImmI32(REG_RSP, (int32_t)stackSize);

    // Epilog: return
    emitRet();
}

static void
processCompileQueue(void)
{
    assert(!codebuf.len);

    // Has the call stub been generated yet?
    size_t callStubOffset;
    if (!callStub) {
        CirArray_grow(&codebuf, sizeof(callStubCode));
        callStubOffset = codebuf.len;
        memcpy(codebuf.items + codebuf.len, callStubCode, sizeof(callStubCode));
        codebuf.len += sizeof(callStubCode);
    }

    // Process compile queue
    CirArray(CirVarId) compiled = CIRARRAY_INIT;
    while (compileQueue.len) {
        CirVarId queuedvar_id = compileQueue.items[compileQueue.len - 1];
        compileQueue.len--;
        doCompile(queuedvar_id);
        CirArray_push(&compiled, &queuedvar_id);
    }

    // Nothing to compile
    if (!codebuf.len && !compiled.len) {
        assert(!needPatch.len); // Should not have anything to patch as well
        return;
    }

    // Check if we have any space to upload codebuf
    if (currentPage && currentPageLen + codebuf.len > currentPageAlloc) {
        // Not enough space. Reset
        currentPage = NULL;
        currentPageLen = 0;
        currentPageAlloc = 0;
    }

    // Allocate a new page
    if (!currentPage) {
        long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize < 0)
            cir_fatal("failed to get page size");
        size_t size = addTrailing(codebuf.len, pageSize);
        assert(size % ((uint64_t)pageSize) == 0);
        currentPage = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (currentPage == (void*)-1)
            cir_fatal("mmap failed");
        currentPageAlloc = size;
        currentPageLen = 0;
    } else {
        // Make memory read-write
        if (mprotect(currentPage, currentPageAlloc, PROT_READ | PROT_WRITE) == -1)
            cir_fatal("failed to make executable page read-write");
    }

    // Now we have the page address, we can convert all ALLOC_COMPILING vars to ALLOC_EXTERNAL
    for (size_t i = 0; i < compiled.len; i++) {
        CirVarId compiled_var = compiled.items[i];
        VarInfo *compiled_varinfo = getVarInfo(compiled_var);
        assert(compiled_varinfo->allocStatus == ALLOC_COMPILING);
        compiled_varinfo->ptr = ((uint8_t *)currentPage) + compiled_varinfo->codeOffset + currentPageLen;
        compiled_varinfo->allocStatus = ALLOC_EXTERNAL;
    }

    // Backpatch all external addresses in
    for (size_t i = 0; i < needPatch.len; i++) {
        size_t loc = needPatch.items[i];
        uint64_t varid = 0;
        varid |= (uint64_t)codebuf.items[loc+0] << 0;
        varid |= (uint64_t)codebuf.items[loc+1] << 8;
        varid |= (uint64_t)codebuf.items[loc+2] << 16;
        varid |= (uint64_t)codebuf.items[loc+3] << 24;
        varid |= (uint64_t)codebuf.items[loc+4] << 32;
        varid |= (uint64_t)codebuf.items[loc+5] << 40;
        varid |= (uint64_t)codebuf.items[loc+6] << 48;
        varid |= (uint64_t)codebuf.items[loc+7] << 56;
        const VarInfo *targetVarinfo = getVarInfo(varid);
        assert(targetVarinfo->allocStatus == ALLOC_EXTERNAL);
        uint64_t ptr = (uint64_t)targetVarinfo->ptr;
        codebuf.items[loc+0] = ptr & 0xff;
        codebuf.items[loc+1] = (ptr >> 8) & 0xff;
        codebuf.items[loc+2] = (ptr >> 16) & 0xff;
        codebuf.items[loc+3] = (ptr >> 24) & 0xff;
        codebuf.items[loc+4] = (ptr >> 32) & 0xff;
        codebuf.items[loc+5] = (ptr >> 40) & 0xff;
        codebuf.items[loc+6] = (ptr >> 48) & 0xff;
        codebuf.items[loc+7] = (ptr >> 56) & 0xff;
    }
    needPatch.len = 0;

    // Backpatch all stmt relative jumps in
    for (size_t i = 0; i < needStmtPatch.len; i++) {
        size_t loc = needStmtPatch.items[i];
        CirStmtId stmt_id = 0;
        stmt_id |= (uint64_t)codebuf.items[loc+0] << 0;
        stmt_id |= (uint64_t)codebuf.items[loc+1] << 8;
        stmt_id |= (uint64_t)codebuf.items[loc+2] << 16;
        stmt_id |= (uint64_t)codebuf.items[loc+3] << 24;
        assert(stmt_id < stmtLocs.len);
        size_t stmtLoc = stmtLocs.items[stmt_id];
        assert(stmtLoc != (size_t)-1);
        // Calculate relative jump
        size_t srcLoc = loc + 4;
        union { int32_t i32; uint32_t u32; } u;
        if (stmtLoc >= srcLoc) {
            // Jump forwards
            size_t diff = stmtLoc - srcLoc;
            assert(diff <= INT_MAX);
            u.i32 = diff;
        } else {
            // Jump backwards
            size_t diff = srcLoc - stmtLoc;
            assert(diff <= INT_MAX);
            u.i32 = diff;
            u.i32 = -u.i32;
        }
        codebuf.items[loc+0] = u.u32 & 0xff;
        codebuf.items[loc+1] = (u.u32 >> 8) & 0xff;
        codebuf.items[loc+2] = (u.u32 >> 16) & 0xff;
        codebuf.items[loc+3] = (u.u32 >> 24) & 0xff;
    }
    needStmtPatch.len = 0;

    CirArray_release(&compiled);

    // Upload code
    memcpy(currentPage + currentPageLen, codebuf.items, codebuf.len);
    currentPageLen += codebuf.len;

#if 0
    FILE *fp = fopen("tmp.bin", "wb");
    fwrite(codebuf.items, codebuf.len, 1, fp);
    fclose(fp);
#endif

    // Make memory executable
    if (mprotect(currentPage, currentPageAlloc, PROT_READ | PROT_EXEC) == -1)
        cir_fatal("mprotect failed");

    if (!callStub) {
        callStub = (void *)(((uint8_t *)currentPage) + callStubOffset);
    }

    // Clear codebuf
    codebuf.len = 0;
}

static bool
isCirCodeType(const CirType *type)
{
    if (!CirType_isNamed(type))
        return false;
    CirTypedefId tid = CirType_getTypedefId(type);
    CirName name = CirTypedef_getName(tid);
    if (strcmp(CirName_cstr(name), "CirCodeId"))
        return false;
    const CirType *actualType = CirType_unroll(type);
    uint32_t ikind = CirType_isInt(actualType);
    return ikind == CIR_IUINT;
}

static CirCodeId
intToCode(uint32_t ikind, uint64_t val)
{
    const CirValue *value = CirValue_ofU64(ikind, val);
    CirCodeId code_id = CirCode_ofExpr(value);
    return code_id;
}

static uint64_t
readValue(const CirValue *value)
{
    if (CirValue_isInt(value)) {
        return CirValue_getU64(value);
    } else if (CirValue_isString(value)) {
        return (uint64_t)CirValue_getString(value);
    } else if (CirValue_isVar(value)) {
        CirVarId target_vid = CirValue_getVar(value);
        VarInfo *varinfo = resolveVar(target_vid);
        uint8_t *loc;
        if (varinfo->allocStatus == ALLOC_EXTERNAL) {
            loc = varinfo->ptr;
        } else if (varinfo->allocStatus == ALLOC_GLOBAL) {
            loc = globalMem + varinfo->offset;
        } else {
            cir_bug("wrong allocStatus");
        }

        // TODO: compute additional offset based on field access

        const CirType *type = CirValue_getType(value);
        size_t typeSize = CirType_sizeof(type, &CirMachine__build);
        switch (typeSize) {
        case 1:
            return *((uint8_t *)loc);
        case 2:
            return *((uint16_t *)loc);
        case 4:
            return *((uint32_t *)loc);
        case 8:
            return *((uint64_t *)loc);
        default:
            cir_bug("unsupported size");
        }
    } else if (CirValue_isMem(value)) {
        cir_bug("TODO: readValue deref");
    } else {
        cir_bug("unhandled case");
    }
}

CirCodeId
CirX64_call(CirVarId vid, const CirCodeId *args, size_t numArgs)
{
    // Do a one-time allocation here.
    // It is important that we do it at the beginning,
    // and only once, to prevent the VarInfo memory from shifting during compilation.
    CirArray_alloc(&varinfos, CirVar_getNum());

    size_t newLen = CirStmt_getNum();
    CirArray_alloc(&stmtLocs, CirStmt_getNum());
    for (size_t i = stmtLocs.len; i < newLen; i++)
        stmtLocs.items[i] = (size_t)-1;
    stmtLocs.len = newLen;

    const CirType *type = CirVar_getType(vid);
    assert(type);
    type = CirType_unroll(type);
    assert(CirType_isFun(type));
    size_t numParams = CirType_getNumParams(type);
    const CirFunParam *params = CirType_getParams(type);

    uint64_t argMem[7] = {};

    if (numArgs > 6)
        cir_bug("TODO: support more than 6 args");

    if (numArgs < numParams)
        cir_fatal("Too little args passed to function");

    if (!CirType_isParamsVa(type) && numArgs > numParams)
        cir_fatal("Too many args passed to function");

    for (size_t i = 0; i < numArgs && i < 6; i++) {
        if (i >= numParams)
            goto is_va;

        const CirType *argType = params[i].type;
        if (isCirCodeType(argType)) {
            // No evaluation needed -- we just pass the CirCodeId as-is to the function
            argMem[i + 1] = args[i];
            continue;
        }

is_va:
        if (!CirCode_isExpr(args[i]))
            cir_fatal("const_eval: argument %llu is not an expression", (unsigned long long)i);

        if (CirCode_getFirstStmt(args[i])) {
            // TODO: actually, it is possible, we just need to JIT-compile the code itself as an anonymous function.
            // It's quite a bit of effort though >:(
            cir_fatal("const_eval: argument %llu is not a constant expression", (unsigned long long)i);
        }

        const CirValue *value = CirCode_getValue(args[i]);
        if (!value)
            cir_fatal("const_eval: argument %llu has no value", (unsigned long long)i);
        if (CirValue_isLval(value)) {
            // We will return later to actually read the value
            CirVarId target_vid = CirValue_getVar(value);
            resolveVar(target_vid);
        }
    }

    // Ensure our result type is OK
    const CirType *returnType = CirType_getBaseType(type);
    const CirType *unrolledReturnType = CirType_unroll(returnType);
    uint32_t ikind = 0;
    if (!isCirCodeType(returnType) && !CirType_isVoid(unrolledReturnType) && !(ikind = CirType_isInt(unrolledReturnType))) {
        CirLog_begin(CIRLOG_FATAL);
        CirLog_print("compile-time eval: return type cannot be converted into CirCode: ");
        CirType_log(returnType, NULL);
        CirLog_end();
        exit(1);
    }

    resolveVar(vid);
    processCompileQueue();

    VarInfo *varinfo = resolveVar(vid);
    assert(varinfo->allocStatus == ALLOC_EXTERNAL);
    argMem[0] = (uint64_t)varinfo->ptr;

    for (size_t i = 0; i < numArgs && i < 6; i++) {
        if (i >= numParams)
            goto is_va2;

        const CirType *argType = params[i].type;
        if (isCirCodeType(argType)) {
            continue;
        }

is_va2:
        ;
        const CirValue *value = CirCode_getValue(args[i]);
        argMem[i + 1] = readValue(value);
    }

    // Call
    assert(callStub);
    assert(callStub != varinfo->ptr);
    uint64_t result = callStub(argMem);

    // Convert result
    if (isCirCodeType(returnType)) {
        return result;
    } else if (CirType_isVoid(unrolledReturnType)) {
        return CirCode_ofExpr(NULL);
    } else {
        return intToCode(ikind, result);
    }
}

void
CirX64_test(void)
{
    FILE *fp = fopen("tmp.bin", "wb");
    fwrite(codebuf.items, codebuf.len, 1, fp);
    fclose(fp);
}
