#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void zget_set_error(struct zget_ctx *ctx, int error, const char *fmt, ...)
{
    va_list ap;
    if (ctx == NULL)
        return;
    ctx->error = error;
    va_start(ap, fmt);
    (void)vsnprintf(ctx->message, sizeof(ctx->message), fmt, ap);
    va_end(ap);
}

bool zget_u64_add(uint64_t a, uint64_t b, uint64_t *result)
{
    /* Archive offsets are hostile input; never rely on unsigned wraparound. */
    if (UINT64_MAX - a < b)
        return false;
    *result = a + b;
    return true;
}

bool zget_range_valid(uint64_t offset, uint64_t length, uint64_t total)
{
    uint64_t end;

    /*
     * Validate the half-open interval [offset, offset + length) in one place.
     * Callers may issue an HTTP request only after both addition and containment
     * succeed, preventing a wrapped ZIP offset from addressing unrelated bytes.
     */
    return zget_u64_add(offset, length, &end) && end <= total;
}

uint16_t zget_le16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t zget_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t zget_le64(const unsigned char *p)
{
    return (uint64_t)zget_le32(p) | ((uint64_t)zget_le32(p + 4) << 32);
}

bool zget_valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i++];
        unsigned need, min;
        uint32_t cp;
        if (c < 0x80)
            continue;
        if (c >= 0xc2 && c <= 0xdf) { need = 1; min = 0x80; cp = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { need = 2; min = 0x800; cp = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { need = 3; min = 0x10000; cp = c & 0x07; }
        else return false;
        if (n - i < need)
            return false;
        while (need--) {
            unsigned d = s[i++];
            if ((d & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (d & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}

char *zget_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy != NULL)
        memcpy(copy, s, n);
    return copy;
}
