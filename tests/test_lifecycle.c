#include "zget.h"

#include <stdio.h>
#include <string.h>

static int discard_output(void *userdata, const void *data, size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return 0;
}

static int expect_uninitialized(void)
{
    zget_ctx *ctx = (zget_ctx *)1;
    int rc = zget_open_url_ex("https://example.invalid/archive.zip", NULL,
                              &ctx);

    if (rc != ZGET_ENOTINITIALIZED || ctx != NULL) {
        fprintf(stderr, "open without initialization returned %d\n", rc);
        return 1;
    }
    if (zget_get("https://example.invalid/archive.zip", "member", discard_output,
                 NULL) != ZGET_ENOTINITIALIZED) {
        fprintf(stderr, "convenience API bypassed global initialization\n");
        return 1;
    }
    if (strcmp(zget_error_string(ZGET_ENOTINITIALIZED),
               "zget is not initialized") != 0) {
        fprintf(stderr, "initialization error has the wrong description\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    /* Extra cleanup is intentionally harmless, matching libcurl's behavior. */
    zget_global_cleanup();
    if (expect_uninitialized() != 0)
        return 1;

    /* Exercise nested owners: both successful acquisitions must be released. */
    if (zget_global_init() != ZGET_OK || zget_global_init() != ZGET_OK) {
        fprintf(stderr, "could not initialize zget\n");
        return 1;
    }
    zget_global_cleanup();
    zget_global_cleanup();

    return expect_uninitialized();
}
