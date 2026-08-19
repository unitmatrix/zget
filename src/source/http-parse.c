#include "source/http-parse.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Parse one unsigned decimal field without accepting signs or overflow. */
static bool parse_u64(const char **cursor, uint64_t *value)
{
    const char *start = *cursor;
    char *end;
    uintmax_t parsed;

    if (!isdigit((unsigned char)*start))
        return false;
    errno = 0;
    parsed = strtoumax(start, &end, 10);
    if (errno == ERANGE || parsed > UINT64_MAX)
        return false;
    *cursor = end;
    *value = (uint64_t)parsed;
    return true;
}

/*
 * Content-Range has an exact grammar; scanf's signs and loose whitespace do
 * not. value is NUL-terminated, matching curl_easy_header() response values.
 */
bool zget_parse_content_range(const char *value, uint64_t *start,
                              uint64_t *end, uint64_t *total)
{
    const char *p = value;

    if (strncmp(p, "bytes ", 6) != 0)
        return false;
    p += 6;
    if (!parse_u64(&p, start) || *p++ != '-' ||
        !parse_u64(&p, end) || *p++ != '/' ||
        !parse_u64(&p, total) || *p != '\0')
        return false;
    return true;
}
