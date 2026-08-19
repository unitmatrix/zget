#include "format/zip/zip-parse.h"
#include "zget.h"
#include <stddef.h>
#include <stdint.h>
/* Exercise the fixed Central Directory parser with arbitrary record bytes. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct zget_cd_fixed out;
    (void)zget_zip_parse_cd_fixed(data, size, &out);
    return 0;
}
