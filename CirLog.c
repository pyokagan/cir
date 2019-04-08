#include "cir_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#define MAX_LOCATIONS 60

typedef struct Location {
    CirName filename;
    uint32_t line;
} Location;

static Location locationStack[MAX_LOCATIONS];
static size_t locationStackTop;
static Location realLocation;
static uint32_t logLevel;
static bool hasNewline;

void
CirLog_begin(uint32_t level)
{
    assert(!logLevel);

    switch (level) {
    case CIRLOG_DEBUG:
        fputs("debug: ", stderr);
        break;
    case CIRLOG_INFO:
        fputs("info: ", stderr);
        break;
    case CIRLOG_WARN:
        fputs("warning: ", stderr);
        break;
    case CIRLOG_ERROR:
        fputs("error: ", stderr);
        break;
    case CIRLOG_FATAL:
        fputs("FATAL: ", stderr);
        break;
    case CIRLOG_BUG:
        fputs("BUG: ", stderr);
        break;
    }
    hasNewline = false;
    logLevel = level;
}

void
CirLog_end(void)
{
    assert(logLevel);

    // Complete newline
    if (!hasNewline)
        fputs("\n", stderr);

    // Print location information
    for (size_t i = 0; i < locationStackTop; i++) {
        fprintf(stderr, "  %s %s:%u\n", !i ? "in" : "included by",
            CirName_cstr(locationStack[locationStackTop - i - 1].filename),
            locationStack[locationStackTop - i - 1].line);
    }
    fprintf(stderr, "  in real location %s:%u\n",
        CirName_cstr(realLocation.filename),
        realLocation.line);

    fflush(stderr);
    logLevel = 0;
}

void
CirLog_printb(const void *s, size_t len)
{
    fwrite(s, len, 1, stderr);
}

void
CirLog_print(const char *s)
{
    fputs(s, stderr);
}

void
CirLog_printf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    CirLog_vprintf(fmt, va);
    va_end(va);
}

void
CirLog_vprintf(const char *fmt, va_list va)
{
    vfprintf(stderr, fmt, va);
}

void
CirLog_printq(const char *s)
{
    CirLog_printqb(s, strlen(s));
}

void
CirLog_printqb(const char *s, size_t len)
{
    fputs("\"", stderr);
    for (size_t i = 0; i < len; i++) {
        uint8_t c = s[i];
        fputs(CirQuote__table[c], stderr);
        if (c >= 127)
            fputs("\"\"", stderr);
    }
    fputs("\"", stderr);
}

void
cir_fatal(const char *fmt, ...)
{
    va_list va;

    CirLog_begin(CIRLOG_FATAL);
    va_start(va, fmt);
    CirLog_vprintf(fmt, va);
    va_end(va);
    CirLog_end();
    exit(1);
}

void
cir_bug(const char *fmt, ...)
{
    va_list va;

    CirLog_begin(CIRLOG_BUG);
    va_start(va, fmt);
    CirLog_vprintf(fmt, va);
    va_end(va);
    CirLog_end();
    abort();
}

void
cir_warn(const char *fmt, ...)
{
    va_list va;

    CirLog_begin(CIRLOG_WARN);
    va_start(va, fmt);
    CirLog_vprintf(fmt, va);
    va_end(va);
    CirLog_end();
}

void
cir_log(const char *fmt, ...)
{
    va_list va;

    CirLog_begin(CIRLOG_DEBUG);
    va_start(va, fmt);
    CirLog_vprintf(fmt, va);
    va_end(va);
    CirLog_end();
}

void
CirLog__announceNewLine(void)
{
    if (locationStackTop) {
        locationStack[locationStackTop - 1].line++;
    }
    realLocation.line++;
}

void
CirLog__pushLocation(CirName filename, uint32_t line)
{
    Location loc = { filename, line };
    if (locationStackTop >= MAX_LOCATIONS)
        cir_fatal("location stack too large");
    locationStack[locationStackTop++] = loc;
}

void
CirLog__popLocation(void)
{
    if (!locationStackTop)
        cir_bug("location stack is empty");
    locationStackTop--;
}

void
CirLog__setLocation(CirName filename, uint32_t line)
{
    if (!locationStackTop)
        cir_bug("location stack is empty");
    locationStack[locationStackTop-1].filename = filename;
    locationStack[locationStackTop-1].line = line;
}

void
CirLog__setRealLocation(CirName filename, uint32_t line)
{
    realLocation.filename = filename;
    realLocation.line = line;
}
