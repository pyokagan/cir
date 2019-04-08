#include "cir_internal.h"
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#define STRING_BUF_SIZE (1024 * 1024 * 1)

static char strbuf[STRING_BUF_SIZE];
static size_t strbufLen;

// The maximum number of tokens we may need to push onto the stack at any time
#define TOK_STACK_SIZE 1

CirToken cirtok;

static CirToken tokenStack[TOK_STACK_SIZE];
static size_t tokenStack_len;

static CirBBuf bbuf;
static size_t idx;

static const CirMachine *CirLex__mach;

void
CirLex__init(const char *path, const CirMachine *mach)
{
    CirLex__mach = mach;
    CirBBuf_init(&bbuf);
    CirBBuf__readFile(&bbuf, path);
    idx = 0;
    CirName filename = CirName_of(path);
    CirLog__setRealLocation(filename, 1);
    CirLog__pushLocation(filename, 1);
}

void
CirLex__push(const CirToken *token)
{
    assert(tokenStack_len < TOK_STACK_SIZE);
    tokenStack[tokenStack_len++] = *token;
}

static bool
CirLex__eof(void)
{
    return idx >= bbuf.len;
}

static uint8_t
CirLex__c(void)
{
    assert(!CirLex__eof());
    return bbuf.items[idx];
}

static void
CirLex__advance(size_t n)
{
    idx += n;
}

static bool
CirLex__startsWith(const char *p)
{
    for (size_t i = 0; *p; i++, p++) {
        if (idx + i >= bbuf.len || bbuf.items[idx + i] != *p)
            return false;
    }

    return true;
}

static bool
CirLex__startsWithI(const char *p)
{
    for (size_t i = 0; *p; i++, p++) {
        if (idx + i >= bbuf.len || toupper(bbuf.items[idx + i]) != *p)
            return false;
    }
    return true;
}

static bool
isoctal(char c)
{
    return '0' <= c && c <= '7';
}

static int
hex(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    assert('A' <= c && c <= 'F');
    return c - 'A' + 10;
}

// Read a single character in a char or string literal
static char
CirLex__char(const char *what)
{
    // Nonescaped
    if (CirLex__c() != '\\') {
        char c = CirLex__c();
        CirLex__advance(1);
        return c;
    }

    CirLex__advance(1); // Consume escape character
    if (CirLex__eof()) {
        cir_fatal("lexer error: unterminated %s literal at EOF", what);
    }

    char c = 0;
    switch (CirLex__c()) {
    // Simple (e.g. '\n' or '\a')
    case 'a':
        CirLex__advance(1);
        return '\a';
    case 'b':
        CirLex__advance(1);
        return '\b';
    case 'f':
        CirLex__advance(1);
        return '\f';
    case 'n':
        CirLex__advance(1);
        return '\n';
    case 'r':
        CirLex__advance(1);
        return '\r';
    case 't':
        CirLex__advance(1);
        return '\t';
    case 'v':
        CirLex__advance(1);
        return '\v';
    case 'e': case 'E':
        CirLex__advance(1);
        return '\033';
    // Hexadecimal
    case 'x':
        CirLex__advance(1);
        while (!CirLex__eof() && isxdigit(CirLex__c())) {
            c = c * 16 + hex(CirLex__c());
            CirLex__advance(1);
        }
        return c;
    // Octal
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        c = CirLex__c() - '0';
        CirLex__advance(1);
        if (!CirLex__eof() && isoctal(CirLex__c())) {
            c = c * 8 + (CirLex__c() - '0');
            CirLex__advance(1);
        }
        if (!CirLex__eof() && isoctal(CirLex__c())) {
            c = c * 8 + (CirLex__c() - '0');
            CirLex__advance(1);
        }
        return c;
    // Default: return as-is
    default:
        c = CirLex__c();
        CirLex__advance(1);
        return c;
    }
}

enum CirLex__suffix {
    CIRLEX_SUFFIX_NONE,
    CIRLEX_SUFFIX_L,
    CIRLEX_SUFFIX_U,
    CIRLEX_SUFFIX_UL,
    CIRLEX_SUFFIX_LL,
    CIRLEX_SUFFIX_ULL
};

static enum CirLex__suffix
CirLex__intsuffix(void)
{
    if (CirLex__startsWithI("ULL") || CirLex__startsWithI("LLU")) {
        CirLex__advance(3);
        return CIRLEX_SUFFIX_ULL;
    } else if (CirLex__startsWithI("LL")) {
        CirLex__advance(2);
        return CIRLEX_SUFFIX_LL;
    } else if (CirLex__startsWithI("UL") || CirLex__startsWithI("LU")) {
        CirLex__advance(2);
        return CIRLEX_SUFFIX_UL;
    } else if (CirLex__startsWithI("L")) {
        CirLex__advance(1);
        return CIRLEX_SUFFIX_L;
    } else if (CirLex__startsWithI("U")) {
        CirLex__advance(1);
        return CIRLEX_SUFFIX_U;
    } else {
        return CIRLEX_SUFFIX_NONE;
    }
}

static bool
fitsInInt(uint64_t val, uint32_t ikind, const CirMachine *mach)
{
    uint32_t sizeInBytes = CirIkind_size(ikind, mach);
    if (CirIkind_isSigned(ikind, mach)) {
        // Signed trunc
        switch (sizeInBytes) {
        case 1:
            return val <= 127ULL;
        case 2:
            return val <= 32767ULL;
        case 4:
            return val <= 2147483647ULL;
        case 8:
            return val <= 9223372036854775807ULL;
        default:
            cir_bug("unsupported sizeInBytes");
        }
    } else {
        // Unsigned trunc
        switch (sizeInBytes) {
        case 1:
            return val <= 255ULL;
        case 2:
            return val <= 65535ULL;
        case 4:
            return val <= 4294967295ULL;
        case 8:
            return true;
        default:
            cir_bug("unsupported sizeInBytes");
        }
    }
}

static bool
fitVal(uint64_t val, bool decimal, enum CirLex__suffix suffix, const CirMachine *mach)
{
    uint32_t totry[6] = {};

    switch (suffix) {
    case CIRLEX_SUFFIX_ULL:
        totry[0] = CIR_IULONGLONG;
        break;
    case CIRLEX_SUFFIX_LL:
        if (!decimal) {
            totry[0] = CIR_ILONGLONG;
            totry[1] = CIR_IULONGLONG;
        } else {
            totry[0] = CIR_ILONGLONG;
        }
        break;
    case CIRLEX_SUFFIX_UL:
        totry[0] = CIR_IULONG;
        totry[1] = CIR_IULONGLONG;
        break;
    case CIRLEX_SUFFIX_L:
        if (!decimal) {
            totry[0] = CIR_ILONG;
            totry[1] = CIR_IULONG;
            totry[2] = CIR_ILONGLONG;
            totry[3] = CIR_IULONGLONG;
        } else {
            totry[0] = CIR_ILONG;
            totry[1] = CIR_ILONGLONG;
        }
        break;
    case CIRLEX_SUFFIX_U:
        totry[0] = CIR_IUINT;
        totry[1] = CIR_IULONG;
        totry[2] = CIR_IULONGLONG;
        break;
    case CIRLEX_SUFFIX_NONE:
        if (!decimal) {
            totry[0] = CIR_IINT;
            totry[1] = CIR_IUINT;
            totry[2] = CIR_ILONG;
            totry[3] = CIR_IULONG;
            totry[4] = CIR_ILONGLONG;
            totry[5] = CIR_IULONGLONG;
        } else {
            totry[0] = CIR_IINT;
            totry[1] = CIR_ILONG;
            totry[2] = CIR_ILONGLONG;
        }
        break;
    default:
        cir_bug("unknown suffix");
    }

    size_t i;
    for (i = 0; i < 7 && totry[i]; i++) {
        if (fitsInInt(val, totry[i], mach)) {
            cirtok.data.intlit.ikind = totry[i];
            if (CirIkind_isSigned(totry[i], mach))
                cirtok.data.intlit.val.i64 = val;
            else
                cirtok.data.intlit.val.u64 = val;
            return false; // no overflow
        }
    }

    cirtok.data.intlit.ikind = totry[i-1];
    if (CirIkind_isSigned(totry[i-1], mach))
        cirtok.data.intlit.val.i64 = val;
    else
        cirtok.data.intlit.val.u64 = val;

    return true; // has overflow
}

static struct {
    const char *name;
    int ty;
} symbols[] = {
    { "<<=", CIRTOK_INF_INF_EQ },
    { ">>=", CIRTOK_SUP_SUP_EQ },
    { "...", CIRTOK_ELLIPSIS },
    { "+=", CIRTOK_PLUS_EQ },
    { "-=", CIRTOK_MINUS_EQ },
    { "*=", CIRTOK_STAR_EQ },
    { "/=", CIRTOK_SLASH_EQ },
    { "%=", CIRTOK_PERCENT_EQ },
    { "|=", CIRTOK_PIPE_EQ },
    { "&=", CIRTOK_AND_EQ },
    { "^=", CIRTOK_CIRC_EQ },
    { "<<", CIRTOK_INF_INF },
    { ">>", CIRTOK_SUP_SUP },
    { "==", CIRTOK_EQ_EQ },
    { "!=", CIRTOK_EXCLAM_EQ },
    { "<=", CIRTOK_INF_EQ },
    { ">=", CIRTOK_SUP_EQ },
    { "++", CIRTOK_PLUS_PLUS },
    { "--", CIRTOK_MINUS_MINUS },
    { "->", CIRTOK_ARROW },
    { "&&", CIRTOK_AND_AND },
    { "||", CIRTOK_PIPE_PIPE },
    { NULL, 0 }
};

// Unlike isspace(), this returns false on \n
static bool
isBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// We are lexing a file hash with format
// # <line number> <file name> <flags>
static void
CirLex__nextFileHash(void)
{
    // We enter the function with the hash already seen,
    // and we are looking at the line number.
    assert(isdigit(CirLex__c()));

    // Parse line number
    uint32_t lineNumber = 0;
    while (!CirLex__eof() && isdigit(CirLex__c())) {
        lineNumber = lineNumber * 10 + (CirLex__c() - '0');
        CirLex__advance(1);
    }

    // Subtract 1 since the lineno only starts after this line
    lineNumber--;

    // Skip whitespace
    while (!CirLex__eof() && isBlank(CirLex__c()))
        CirLex__advance(1);

    // Are we looking at the start of the filename?
    if (CirLex__eof() || CirLex__c() != '"') {
        // Unclear what we are looking at
        while (!CirLex__eof() && CirLex__c() != '\n')
            CirLex__advance(1);
        return;
    }

    // Parse filename
    CirLex__advance(1); // consume `"`
    strbufLen = 0;
    while (!CirLex__eof() && CirLex__c() != '"') {
        char c = CirLex__char("string");
        if (c == '\0')
            cir_fatal("lexer error: hash filename cannot contain NUL bytes");
        if (strbufLen >= STRING_BUF_SIZE)
            cir_fatal("lexer error: string literal is too long");
        strbuf[strbufLen++] = c;
    }
    if (CirLex__eof())
        cir_fatal("lexer error: unterminated string at eof");
    assert(CirLex__c() == '"');
    CirLex__advance(1);
    // NUL-terminate filename string
    if (strbufLen >= STRING_BUF_SIZE)
        cir_fatal("lexer error: string literal is too long");
    strbuf[strbufLen++] = '\0';
    CirName filename = CirName_of(strbuf);

    // Skip whitespace
    while (!CirLex__eof() && isBlank(CirLex__c()))
        CirLex__advance(1);

    // Parse flags
    bool shouldPush = false, shouldPop = false;
    while (!CirLex__eof() && isdigit(CirLex__c())) {
        if (CirLex__c() == '1') {
            CirLex__advance(1);
            shouldPush = true;
            while (!CirLex__eof() && isBlank(CirLex__c()))
                CirLex__advance(1);
        } else if (CirLex__c() == '2') {
            CirLex__advance(1);
            shouldPop = true;
            while (!CirLex__eof() && isBlank(CirLex__c()))
                CirLex__advance(1);
        } else {
            // Unclear what we are looking at, advance to next digit
            while (!CirLex__eof() && !isBlank(CirLex__c()) && CirLex__c() != '\n')
                CirLex__advance(1);
            while (!CirLex__eof() && !isdigit(CirLex__c()) && CirLex__c() != '\n')
                CirLex__advance(1);
        }
    }

    if (shouldPush) {
        CirLog__pushLocation(filename, lineNumber);
    } else if (shouldPop) {
        CirLog__popLocation();
        CirLog__setLocation(filename, lineNumber);
    } else {
        CirLog__setLocation(filename, lineNumber);
    }

    assert(CirLex__eof() || CirLex__c() == '\n');
    return;
}

void
CirLex__next(void)
{
    if (tokenStack_len) {
        cirtok = tokenStack[--tokenStack_len];
        return;
    }

loop:
    if (CirLex__eof()) {
        // EOF
        cirtok.type = CIRTOK_EOF;
        return;
    } else if (CirLex__c() == '\n') {
        // Newline
        CirLog__announceNewLine();
        CirLex__advance(1);
        goto loop;
    } else if (isBlank(CirLex__c())) {
        // Skip whitespace
        CirLex__advance(1);
        goto loop;
    } else if (CirLex__startsWith("//")) {
        // Line comment
        CirLex__advance(strlen("//"));
        while (!CirLex__eof() && CirLex__c() != '\n')
            CirLex__advance(1);
        goto loop;
    } else if (CirLex__startsWith("/*")) {
        // Block comment
        CirLex__advance(strlen("/*"));
        for (;;) {
            if (CirLex__eof()) {
                cir_fatal("lexer error: unterminated block comment at EOF");
            } else if (CirLex__startsWith("*/")) {
                CirLex__advance(strlen("*/"));
                break;
            } else if (CirLex__c() == '\n') {
                CirLog__announceNewLine();
                CirLex__advance(1);
            } else {
                CirLex__advance(1);
            }
        }
        goto loop;
    } else if (CirLex__c() == '\'') {
        // Character literal
        CirLex__advance(1);
        char val = CirLex__char("character");
        if (CirLex__eof() || CirLex__c() != '\'')
            cir_fatal("lexer error: unclosed character literal");
        CirLex__advance(1);
        cirtok.type = CIRTOK_CHARLIT;
        cirtok.data.charlit = val;
        return;
    } else if (CirLex__c() == '"') {
        // String literal
        CirLex__advance(1);
        strbufLen = 0;
        while (!CirLex__eof() && CirLex__c() != '"') {
            char c = CirLex__char("string");
            if (strbufLen >= STRING_BUF_SIZE)
                cir_fatal("lexer error: string literal is too long");
            strbuf[strbufLen++] = c;
        }
        if (CirLex__eof())
            cir_fatal("lexer error: unterminated string at eof");
        assert(CirLex__c() == '"');
        CirLex__advance(1);
        cirtok.type = CIRTOK_STRINGLIT;
        cirtok.data.stringlit.buf = strbuf;
        cirtok.data.stringlit.len = strbufLen;
        return;
    } else if (CirLex__startsWith("R\"")) {
        // Raw string literal
        char delim[19];
        delim[0] = ')';
        CirLex__advance(2); // consume R"
        size_t i = 1;
        for (; !CirLex__eof() && CirLex__c() != '('; CirLex__advance(1), i++) {
            if (i >= 17)
                cir_fatal("lexer error: raw string delimiter too long");
            char c = CirLex__c();
            if (c == ')' || c == '\\' || isspace(c))
                cir_fatal("lexer error: invalid raw string delimiter character: %c", c);
            delim[i] = c;
        }
        if (CirLex__eof())
            cir_fatal("lexer error: unterminated raw string at eof");
        delim[i++] = '"';
        size_t delimLen = i;
        delim[i++] = '\0';
        strbufLen = 0;
        assert(CirLex__c() == '(');
        CirLex__advance(1); // consume (
        while (!CirLex__eof() && !CirLex__startsWith(delim)) {
            char c = CirLex__c();
            if (strbufLen >= STRING_BUF_SIZE)
                cir_fatal("lexer error: string literal is too long");
            strbuf[strbufLen++] = c;
            CirLex__advance(1);
        }
        if (CirLex__eof())
            cir_fatal("lexer error: unterminated raw string at eof");
        CirLex__advance(delimLen);
        cirtok.type = CIRTOK_STRINGLIT;
        cirtok.data.stringlit.buf = strbuf;
        cirtok.data.stringlit.len = strbufLen;
        return;
    } else if (CirLex__startsWith("0x") || CirLex__startsWith("0X")) {
        // Hexadecimal number
        CirLex__advance(2);
        if (CirLex__eof() || !isxdigit(CirLex__c()))
            cir_fatal("lexer error: bad hexadecimal number");
        uint64_t val = 0;
        bool hasOverflow = false;
        while (!CirLex__eof() && isxdigit(CirLex__c())) {
            if (__builtin_mul_overflow(val, (uint64_t)16, &val))
                hasOverflow = true;
            uint64_t b = hex(CirLex__c());
            CirLex__advance(1);
            if (__builtin_add_overflow(val, b, &val))
                hasOverflow = true;
        }
        cirtok.type = CIRTOK_INTLIT;
        enum CirLex__suffix suffix = CirLex__intsuffix();
        if (fitVal(val, false, suffix, CirLex__mach))
            hasOverflow = true;
        if (hasOverflow)
            cir_warn("hex literal: overflow");
        return;
    } else if (CirLex__c() == '0') {
        // Octal number
        uint64_t val = 0;
        bool hasOverflow = false;
        for (int i = 0; i < 3 && !CirLex__eof() && isoctal(CirLex__c()); i++) {
            if (__builtin_mul_overflow(val, 8, &val))
                hasOverflow = true;
            uint64_t b = CirLex__c() - '0';
            CirLex__advance(1);
            if (__builtin_add_overflow(val, b, &val))
                hasOverflow = true;
        }
        cirtok.type = CIRTOK_INTLIT;
        enum CirLex__suffix suffix = CirLex__intsuffix();
        if (fitVal(val, false, suffix, CirLex__mach))
            hasOverflow = true;
        if (hasOverflow)
            cir_warn("octal literal: overflow");
        return;
    } else if (isdigit(CirLex__c())) {
        // Decimal number
        uint64_t val = 0;
        bool hasOverflow = false;
        while (!CirLex__eof() && isdigit(CirLex__c())) {
            if (__builtin_mul_overflow(val, 10, &val))
                hasOverflow = true;
            uint64_t b = CirLex__c() - '0';
            CirLex__advance(1);
            if (__builtin_add_overflow(val, b, &val))
                hasOverflow = true;
        }
        cirtok.type = CIRTOK_INTLIT;
        enum CirLex__suffix suffix = CirLex__intsuffix();
        if (fitVal(val, true, suffix, CirLex__mach))
            hasOverflow = true;
        if (hasOverflow)
            cir_warn("decimal literal: overflow");
        return;
    } else if (CirLex__c() == '#') {
        // Pragma or Line info
        CirLex__advance(1); // consume #

        if (CirLex__eof())
            goto loop;

        // Skip whitespace
        while (!CirLex__eof() && isBlank(CirLex__c()))
            CirLex__advance(1);

        if (CirLex__eof())
            goto loop;

        if (isdigit(CirLex__c())) {
            // We are looking at a file hash line
            CirLex__nextFileHash();
        } else {
            // Unclear what we are looking at,
            // skip until end of line/EOF
            while (!CirLex__eof() && CirLex__c() != '\n')
                CirLex__advance(1);
        }
        goto loop;
    }

    // Is an operator with more than one symbol?
    for (size_t i = 0; symbols[i].name; i++) {
        if (CirLex__startsWith(symbols[i].name)) {
            cirtok.type = symbols[i].ty;
            CirLex__advance(strlen(symbols[i].name));
            return;
        }
    }

    // Is an operator with a single symbol?
    switch (CirLex__c()) {
    case '=':
        cirtok.type = CIRTOK_EQ;
        CirLex__advance(1);
        return;
    case '<':
        cirtok.type = CIRTOK_INF;
        CirLex__advance(1);
        return;
    case '>':
        cirtok.type = CIRTOK_SUP;
        CirLex__advance(1);
        return;
    case '+':
        cirtok.type = CIRTOK_PLUS;
        CirLex__advance(1);
        return;
    case '-':
        cirtok.type = CIRTOK_MINUS;
        CirLex__advance(1);
        return;
    case '*':
        cirtok.type = CIRTOK_STAR;
        CirLex__advance(1);
        return;
    case '/':
        cirtok.type = CIRTOK_SLASH;
        CirLex__advance(1);
        return;
    case '%':
        cirtok.type = CIRTOK_PERCENT;
        CirLex__advance(1);
        return;
    case '!':
        cirtok.type = CIRTOK_EXCLAM;
        CirLex__advance(1);
        return;
    case '&':
        cirtok.type = CIRTOK_AND;
        CirLex__advance(1);
        return;
    case '|':
        cirtok.type = CIRTOK_PIPE;
        CirLex__advance(1);
        return;
    case '^':
        cirtok.type = CIRTOK_CIRC;
        CirLex__advance(1);
        return;
    case '?':
        cirtok.type = CIRTOK_QUEST;
        CirLex__advance(1);
        return;
    case ':':
        cirtok.type = CIRTOK_COLON;
        CirLex__advance(1);
        return;
    case '~':
        cirtok.type = CIRTOK_TILDE;
        CirLex__advance(1);
        return;
    case '{':
        cirtok.type = CIRTOK_LBRACE;
        CirLex__advance(1);
        return;
    case '}':
        cirtok.type = CIRTOK_RBRACE;
        CirLex__advance(1);
        return;
    case '[':
        cirtok.type = CIRTOK_LBRACKET;
        CirLex__advance(1);
        return;
    case ']':
        cirtok.type = CIRTOK_RBRACKET;
        CirLex__advance(1);
        return;
    case '(':
        cirtok.type = CIRTOK_LPAREN;
        CirLex__advance(1);
        return;
    case ')':
        cirtok.type = CIRTOK_RPAREN;
        CirLex__advance(1);
        return;
    case ';':
        cirtok.type = CIRTOK_SEMICOLON;
        CirLex__advance(1);
        return;
    case ',':
        cirtok.type = CIRTOK_COMMA;
        CirLex__advance(1);
        return;
    case '.':
        cirtok.type = CIRTOK_DOT;
        CirLex__advance(1);
        return;
    case '@':
        cirtok.type = CIRTOK_AT;
        CirLex__advance(1);
        return;
    }

    // Is a keyword or identifier?
    if (isalpha(CirLex__c()) || CirLex__c() == '_') {
        // Read ident into buffer
        strbufLen = 0;
        while (!CirLex__eof() && (isalpha(CirLex__c()) || isdigit(CirLex__c()) || CirLex__c() == '_')) {
            if (strbufLen >= STRING_BUF_SIZE)
                cir_fatal("lexer error: ident is too long");
            strbuf[strbufLen++] = CirLex__c();
            CirLex__advance(1);
        }
        // NUL-terminate string buffer
        if (strbufLen >= STRING_BUF_SIZE)
            cir_fatal("lexer error: ident is too long");
        strbuf[strbufLen++] = '\0';

        // Is this a reserved keyword?
        if (!strcmp(strbuf, "auto")) {
            cirtok.type = CIRTOK_AUTO;
            return;
        } else if (!strcmp(strbuf, "const")) {
            cirtok.type = CIRTOK_CONST;
            return;
        } else if (!strcmp(strbuf, "static")) {
            cirtok.type = CIRTOK_STATIC;
            return;
        } else if (!strcmp(strbuf, "extern")) {
            cirtok.type = CIRTOK_EXTERN;
            return;
        } else if (!strcmp(strbuf, "long")) {
            cirtok.type = CIRTOK_LONG;
            return;
        } else if (!strcmp(strbuf, "short")) {
            cirtok.type = CIRTOK_SHORT;
            return;
        } else if (!strcmp(strbuf, "register")) {
            cirtok.type = CIRTOK_REGISTER;
            return;
        } else if (!strcmp(strbuf, "signed")) {
            cirtok.type = CIRTOK_SIGNED;
            return;
        } else if (!strcmp(strbuf, "unsigned")) {
            cirtok.type = CIRTOK_UNSIGNED;
            return;
        } else if (!strcmp(strbuf, "volatile")) {
            cirtok.type = CIRTOK_VOLATILE;
            return;
        } else if (!strcmp(strbuf, "_Bool")) {
            cirtok.type = CIRTOK_BOOL;
            return;
        } else if (!strcmp(strbuf, "char")) {
            cirtok.type = CIRTOK_CHAR;
            return;
        } else if (!strcmp(strbuf, "int")) {
            cirtok.type = CIRTOK_INT;
            return;
        } else if (!strcmp(strbuf, "float")) {
            cirtok.type = CIRTOK_FLOAT;
            return;
        } else if (!strcmp(strbuf, "double")) {
            cirtok.type = CIRTOK_DOUBLE;
            return;
        } else if (!strcmp(strbuf, "void")) {
            cirtok.type = CIRTOK_VOID;
            return;
        } else if (!strcmp(strbuf, "enum")) {
            cirtok.type = CIRTOK_ENUM;
            return;
        } else if (!strcmp(strbuf, "struct")) {
            cirtok.type = CIRTOK_STRUCT;
            return;
        } else if (!strcmp(strbuf, "typedef")) {
            cirtok.type = CIRTOK_TYPEDEF;
            return;
        } else if (!strcmp(strbuf, "union")) {
            cirtok.type = CIRTOK_UNION;
            return;
        } else if (!strcmp(strbuf, "break")) {
            cirtok.type = CIRTOK_BREAK;
            return;
        } else if (!strcmp(strbuf, "continue")) {
            cirtok.type = CIRTOK_CONTINUE;
            return;
        } else if (!strcmp(strbuf, "goto")) {
            cirtok.type = CIRTOK_GOTO;
            return;
        } else if (!strcmp(strbuf, "return")) {
            cirtok.type = CIRTOK_RETURN;
            return;
        } else if (!strcmp(strbuf, "switch")) {
            cirtok.type = CIRTOK_SWITCH;
            return;
        } else if (!strcmp(strbuf, "case")) {
            cirtok.type = CIRTOK_CASE;
            return;
        } else if (!strcmp(strbuf, "default")) {
            cirtok.type = CIRTOK_DEFAULT;
            return;
        } else if (!strcmp(strbuf, "while")) {
            cirtok.type = CIRTOK_WHILE;
            return;
        } else if (!strcmp(strbuf, "do")) {
            cirtok.type = CIRTOK_DO;
            return;
        } else if (!strcmp(strbuf, "for")) {
            cirtok.type = CIRTOK_FOR;
            return;
        } else if (!strcmp(strbuf, "if")) {
            cirtok.type = CIRTOK_IF;
            return;
        } else if (!strcmp(strbuf, "else")) {
            cirtok.type = CIRTOK_ELSE;
            return;
        } else if (!strcmp(strbuf, "__auto_type")) {
            cirtok.type = CIRTOK_AUTO_TYPE;
            return;
        } else if (!strcmp(strbuf, "inline") || !strcmp(strbuf, "__inline__") || !strcmp(strbuf, "__inline")) {
            cirtok.type = CIRTOK_INLINE;
            return;
        } else if (!strcmp(strbuf, "__attribute__")) {
            cirtok.type = CIRTOK_ATTRIBUTE;
            return;
        } else if (!strcmp(strbuf, "__asm__")) {
            cirtok.type = CIRTOK_ASM;
            return;
        } else if (!strcmp(strbuf, "typeof")) {
            cirtok.type = CIRTOK_TYPEOF;
            return;
        } else if (!strcmp(strbuf, "restrict") || !strcmp(strbuf, "__restrict")) {
            cirtok.type = CIRTOK_RESTRICT;
            return;
        } else if (!strcmp(strbuf, "__builtin_va_list")) {
            cirtok.type = CIRTOK_BUILTIN_VA_LIST;
            return;
        } else if (!strcmp(strbuf, "sizeof")) {
            cirtok.type = CIRTOK_SIZEOF;
            return;
        } else if (!strcmp(strbuf, "__extension__")) {
            // Ignore. __extension__ is used to indicate that the declaration/expression
            // should be handled in GNU-C mode.
            // However, we already try to support GNU C extensions whether __extension__ is used or not.
            goto loop;
        } else if (!strcmp(strbuf, "_Alignof") || !strcmp(strbuf, "__alignof__")) {
            cirtok.type = CIRTOK_ALIGNOF;
            return;
        } else if (!strcmp(strbuf, "__typeval")) {
            cirtok.type = CIRTOK_TYPEVAL;
            return;
        } else if (!strcmp(strbuf, "_Float128")) {
            cirtok.type = CIRTOK_FLOAT128;
            return;
        }

        // Convert to name
        CirName name = CirName_of(strbuf);
        CirVarId vid;
        CirTypedefId tid;
        CirEnumItemId enumItemId;
        CirBuiltinId builtinId;

        if ((builtinId = CirBuiltin_ofName(name))) {
            // Is a builtin
            cirtok.type = CIRTOK_BUILTIN;
            cirtok.data.builtinId = builtinId;
        } else if (CirEnv__findLocalName(name, &vid, &tid, &enumItemId) == 2) {
            // Is a type
            cirtok.type = CIRTOK_TYPENAME;
            cirtok.data.name = name;
        } else {
            // Is an ident / enum item
            cirtok.type = CIRTOK_IDENT;
            cirtok.data.name = name;
        }
        return;
    }

    cir_fatal("lexer error: invalid byte: %u", (unsigned int)CirLex__c());
}

const char *
CirLex__str(uint32_t tt)
{
    enum CirTokType t = tt;
    switch (t) {
    case CIRTOK_NONE:
        return "NONE";
    case CIRTOK_EOF:
        return "EOF";
    case CIRTOK_IDENT:
        return "IDENT";
    case CIRTOK_TYPENAME:
        return "TYPENAME";
    case CIRTOK_BUILTIN:
        return "BUILTIN";
    case CIRTOK_STRINGLIT:
        return "STRINGLIT";
    case CIRTOK_CHARLIT:
        return "CHARLIT";
    case CIRTOK_INTLIT:
        return "INTLIT";
    case CIRTOK_INF_INF_EQ:
        return "`<<=`";
    case CIRTOK_SUP_SUP_EQ:
        return "`>>=`";
    case CIRTOK_ELLIPSIS:
        return "`...`";
    case CIRTOK_PLUS_EQ:
        return "`+=`";
    case CIRTOK_MINUS_EQ:
        return "`-=`";
    case CIRTOK_STAR_EQ:
        return "`*=`";
    case CIRTOK_SLASH_EQ:
        return "`/=`";
    case CIRTOK_PERCENT_EQ:
        return "`%=`";
    case CIRTOK_PIPE_EQ:
        return "`|=`";
    case CIRTOK_AND_EQ:
        return "`&=`";
    case CIRTOK_CIRC_EQ:
        return "`^=`";
    case CIRTOK_INF_INF:
        return "`<<`";
    case CIRTOK_SUP_SUP:
        return "`>>`";
    case CIRTOK_EQ_EQ:
        return "`==`";
    case CIRTOK_EXCLAM_EQ:
        return "`!=`";
    case CIRTOK_INF_EQ:
        return "`<=`";
    case CIRTOK_SUP_EQ:
        return "`>=`";
    case CIRTOK_PLUS_PLUS:
        return "`++`";
    case CIRTOK_MINUS_MINUS:
        return "`--`";
    case CIRTOK_ARROW:
        return "`->`";
    case CIRTOK_AND_AND:
        return "`&&`";
    case CIRTOK_PIPE_PIPE:
        return "`||`";
    case CIRTOK_EQ:
        return "`=`";
    case CIRTOK_INF:
        return "`<`";
    case CIRTOK_SUP:
        return "`>`";
    case CIRTOK_PLUS:
        return "`+`";
    case CIRTOK_MINUS:
        return "`-`";
    case CIRTOK_STAR:
        return "`*`";
    case CIRTOK_SLASH:
        return "`/`";
    case CIRTOK_PERCENT:
        return "`%`";
    case CIRTOK_EXCLAM:
        return "`!`";
    case CIRTOK_AND:
        return "`&`";
    case CIRTOK_PIPE:
        return "`|`";
    case CIRTOK_CIRC:
        return "`^`";
    case CIRTOK_QUEST:
        return "`?`";
    case CIRTOK_COLON:
        return "`:`";
    case CIRTOK_TILDE:
        return "`~`";
    case CIRTOK_LBRACE:
        return "`{`";
    case CIRTOK_RBRACE:
        return "`}`";
    case CIRTOK_LBRACKET:
        return "`[`";
    case CIRTOK_RBRACKET:
        return "`]`";
    case CIRTOK_LPAREN:
        return "`(`";
    case CIRTOK_RPAREN:
        return "`)`";
    case CIRTOK_SEMICOLON:
        return "`;`";
    case CIRTOK_COMMA:
        return "`,`";
    case CIRTOK_DOT:
        return "`.`";
    case CIRTOK_AT:
        return "`@`";
    case CIRTOK_AUTO:
        return "AUTO";
    case CIRTOK_CONST:
        return "CONST";
    case CIRTOK_STATIC:
        return "STATIC";
    case CIRTOK_EXTERN:
        return "EXTERN";
    case CIRTOK_LONG:
        return "LONG";
    case CIRTOK_SHORT:
        return "SHORT";
    case CIRTOK_REGISTER:
        return "REGISTER";
    case CIRTOK_SIGNED:
        return "SIGNED";
    case CIRTOK_UNSIGNED:
        return "UNSIGNED";
    case CIRTOK_VOLATILE:
        return "VOLATILE";
    case CIRTOK_BOOL:
        return "BOOL";
    case CIRTOK_CHAR:
        return "CHAR";
    case CIRTOK_INT:
        return "INT";
    case CIRTOK_FLOAT:
        return "FLOAT";
    case CIRTOK_DOUBLE:
        return "DOUBLE";
    case CIRTOK_VOID:
        return "VOID";
    case CIRTOK_ENUM:
        return "ENUM";
    case CIRTOK_STRUCT:
        return "STRUCT";
    case CIRTOK_TYPEDEF:
        return "TYPEDEF";
    case CIRTOK_UNION:
        return "UNION";
    case CIRTOK_BREAK:
        return "BREAK";
    case CIRTOK_CONTINUE:
        return "CONTINUE";
    case CIRTOK_GOTO:
        return "GOTO";
    case CIRTOK_RETURN:
        return "RETURN";
    case CIRTOK_SWITCH:
        return "SWITCH";
    case CIRTOK_CASE:
        return "CASE";
    case CIRTOK_DEFAULT:
        return "DEFAULT";
    case CIRTOK_WHILE:
        return "WHILE";
    case CIRTOK_DO:
        return "DO";
    case CIRTOK_FOR:
        return "FOR";
    case CIRTOK_IF:
        return "IF";
    case CIRTOK_ELSE:
        return "ELSE";
    case CIRTOK_AUTO_TYPE:
        return "AUTO_TYPE";
    case CIRTOK_INLINE:
        return "INLINE";
    case CIRTOK_ATTRIBUTE:
        return "ATTRIBUTE";
    case CIRTOK_ASM:
        return "ASM";
    case CIRTOK_TYPEOF:
        return "TYPEOF";
    case CIRTOK_ALIGNOF:
        return "ALIGNOF";
    case CIRTOK_RESTRICT:
        return "RESTRICT";
    case CIRTOK_BUILTIN_VA_LIST:
        return "BUILTIN_VA_LIST";
    case CIRTOK_SIZEOF:
        return "SIZEOF";
    case CIRTOK_TYPEVAL:
        return "TYPEVAL";
    case CIRTOK_FLOAT128:
        return "_Float128";
    default:
        cir_bug("invalid token type");
    }
}