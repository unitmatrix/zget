#include "format/zip/zip-parse.h"
#include "zget.h"
#include <stddef.h>
#include <stdint.h>
/* Exercise the fixed Local Header parser with arbitrary record bytes. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct zget_local_fixed out;
    (void)zget_zip_parse_local_fixed(data, size, &out);
    return 0;
}
