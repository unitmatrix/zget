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
    return failed ? 1 : 0;
}
