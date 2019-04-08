#include "cir_internal.h"
#include <assert.h>

#define MAX_SCOPES 20

typedef struct LoopScope {
    CirStmtId continueStmtId;
    CirStmtId breakStmtId;
} LoopScope;

static LoopScope scopes[MAX_SCOPES];
static uint32_t scopeStackTop;

void
CirLoopEnv_pushLoop(CirStmtId continueStmtId, CirStmtId breakStmtId)
{
    assert(continueStmtId);
    assert(breakStmtId);

    if (scopeStackTop >= MAX_SCOPES)
        cir_fatal("too many nested loops/switches");

    scopes[scopeStackTop].continueStmtId = continueStmtId;
    scopes[scopeStackTop].breakStmtId = breakStmtId;
    scopeStackTop++;
}

void
CirLoopEnv_pushSwitch(CirStmtId breakStmtId)
{
    assert(breakStmtId);

    if (scopeStackTop >= MAX_SCOPES)
        cir_fatal("too many nested loops/switches");

    scopes[scopeStackTop].continueStmtId = CirLoopEnv_getContinueStmtId();
    scopes[scopeStackTop].breakStmtId = breakStmtId;
    scopeStackTop++;
}

void
CirLoopEnv_pop(void)
{
    if (!scopeStackTop)
        cir_bug("no more loop scopes to pop");
    scopeStackTop--;
}

CirStmtId
CirLoopEnv_getContinueStmtId(void)
{
    if (!scopeStackTop)
        return 0;

    return scopes[scopeStackTop - 1].continueStmtId;
}

CirStmtId
CirLoopEnv_getBreakStmtId(void)
{
    if (!scopeStackTop)
        return 0;

    return scopes[scopeStackTop - 1].breakStmtId;
}
