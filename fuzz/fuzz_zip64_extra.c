#include "format/zip/zip-parse.h"
#include "zget.h"
#include <stddef.h>
#include <stdint.h>
/* Exercise ZIP64 extra-field iteration, ordering, and truncation handling. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t a, b, c;
    uint32_t d;
    /* Saturated legacy fields force the parser to consume every ZIP64 value. */
    (void)zget_zip_parse_zip64_extra(data, size, UINT32_MAX, UINT32_MAX,
                                     UINT32_MAX, UINT16_MAX, &a, &b, &c, &d);
    return 0;
}
