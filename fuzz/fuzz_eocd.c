#include "format/zip/zip-parse.h"
#include "zget.h"
#include <stddef.h>
#include <stdint.h>
/* Exercise both terminal ZIP32 and fixed ZIP64 tail-record parsers. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct zget_eocd out;
    /* Tail bytes can independently resemble the terminal EOCD or ZIP64 record. */
    (void)zget_zip_parse_eocd(data, size, 0, size, &out);
    if (size >= 56)
        (void)zget_zip_parse_zip64(data, size, &out);
    return 0;
}
