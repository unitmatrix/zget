#include "zget.h"

#include <stdio.h>
#include <string.h>

static int bytes_are_zero(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t i;

    for (i = 0; i < size; ++i)
        if (bytes[i] != 0)
            return 0;
    return 1;
}

static int discard(void *userdata, const void *data, size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return 0;
}

static int expect_entry_v1_layout(void)
{
    /* Existing binaries pass this structure by address, so offsets are ABI. */
    if (sizeof(zget_entry) != 32 ||
        offsetof(zget_entry, compressed_size) != 0 ||
        offsetof(zget_entry, uncompressed_size) != 8 ||
        offsetof(zget_entry, local_header_offset) != 16 ||
        offsetof(zget_entry, crc32) != 24 ||
        offsetof(zget_entry, compression_method) != 28 ||
        offsetof(zget_entry, flags) != 30) {
        fprintf(stderr, "zget_entry v0.1 ABI layout changed\n");
        return 1;
    }
    return 0;
}

static int expect_open_clears_context(const char *url)
{
    /* A non-NULL sentinel makes failure to replace the caller's value visible. */
    zget_ctx *ctx = (zget_ctx *)1;
    int rc = zget_open_url_ex(url, NULL, &ctx);

    if (rc != ZGET_EINVAL || ctx != NULL) {
        fprintf(stderr, "invalid URL returned %d and context %p\n",
                rc, (void *)ctx);
        return 1;
    }
    return 0;
}

static int expect_find_clears_entry(void)
{
    zget_entry entry;
    int rc;

    /* Fill every byte, including padding, so a partial clear cannot pass. */
    memset(&entry, 0xa5, sizeof(entry));
    rc = zget_find(NULL, "member", &entry);
    if (rc != ZGET_EINVAL || !bytes_are_zero(&entry, sizeof(entry))) {
        fprintf(stderr, "invalid lookup returned %d with stale entry data\n", rc);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    /* These fail before global initialization and must still sanitize output. */
    failed |= expect_open_clears_context(NULL);
    failed |= expect_open_clears_context("");
    failed |= expect_find_clears_entry();
    failed |= expect_entry_v1_layout();
    if (zget_extract_member(NULL, "member", discard, NULL) != ZGET_EINVAL) {
        fprintf(stderr, "extract-member accepted a NULL context\n");
        failed = 1;
    }
    return failed ? 1 : 0;
}
