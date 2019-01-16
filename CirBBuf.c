#include "cir_internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

void
CirBBuf__add(CirBBuf *buf, const uint8_t *src, size_t len)
{
    CirBBuf_grow(buf, len);
    memcpy(buf->items + buf->len, src, len);
    buf->len += len;
}

static void
CirBBuf__read(CirBBuf *buf, FILE *fp)
{
    CirBBuf_grow(buf, 8192);
    while (!feof(fp)) {
        size_t reqLen = buf->alloc - buf->len;
        size_t bytesRead = fread(buf->items + buf->len, 1, reqLen, fp);
        buf->len += bytesRead;
        if (ferror(fp))
            cir_fatal("failed to read from file: %s", strerror(errno));
        CirBBuf_grow(buf, 8192);
    }
}

void
CirBBuf__readFile(CirBBuf *buf, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        cir_fatal("could not open %s: %s", path, strerror(errno));
    CirBBuf__read(buf, fp);
    fclose(fp);
}
