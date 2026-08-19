#include "format/zip/zip-parse.h"
#include "zget.h"
#include <stddef.h>
#include <stdint.h>
/* Exercise the streaming parser with both arbitrary bytes and fragmentation. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Reserve one byte for callback granularity; the rest is parser input. */
    size_t chunk = size == 0 ? 1 : (size_t)(data[0] % 64u) + 1u;
    if (size != 0)
        (void)zget_zip_fuzz_cd_stream(data + 1, size - 1, chunk);
    return 0;
}
