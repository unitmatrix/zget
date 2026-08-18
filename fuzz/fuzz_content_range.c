#include "internal.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/*
 * zget_parse_content_range() expects a NUL-terminated string, matching what
 * curl_easy_header() always provides for a real response. libFuzzer input is
 * not NUL-terminated, so copy it into a terminated buffer before parsing;
 * anything else would read past the fuzz buffer rather than test the parser.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t start, end, total;
    char *value = malloc(size + 1);
    if (value == NULL)
        return 0;
    memcpy(value, data, size);
    value[size] = '\0';
    (void)zget_parse_content_range(value, &start, &end, &total);
    free(value);
    return 0;
}
