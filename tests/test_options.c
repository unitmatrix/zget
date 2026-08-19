#include "zget.h"

#include <stdio.h>
#include <string.h>

struct future_options {
    zget_options known;
    uint64_t field_unknown_to_this_library;
};

static int expect_option_error(zget_options *options, const char *message)
{
    zget_ctx *ctx = NULL;
    int rc = zget_open_url_ex("file:///not-an-http-url", options, &ctx);
    const char *actual = ctx == NULL ? "" : zget_last_error_message(ctx);
    int failed = rc != ZGET_EINVAL || strcmp(actual, message) != 0;

    if (failed)
        fprintf(stderr, "expected '%s', got error %d: '%s'\n",
                message, rc, actual);
    zget_close(ctx);
    return failed;
}

int main(void)
{
    zget_options options;
    struct future_options future;
    int failed = 0;

    if (ZGET_OPTIONS_V1_SIZE > sizeof(options)) {
        fprintf(stderr, "v1 options boundary exceeds the current structure\n");
        return 1;
    }
    if (zget_global_init() != ZGET_OK) {
        fprintf(stderr, "could not initialize zget\n");
        return 1;
    }

    zget_options_init(&options);
    options.struct_size = ZGET_OPTIONS_V1_SIZE - 1;
    failed |= expect_option_error(&options, "zget_options struct is too small");

    /* The original ABI boundary remains valid after future fields are added. */
    zget_options_init(&options);
    options.struct_size = ZGET_OPTIONS_V1_SIZE;
    failed |= expect_option_error(&options, "URL must use HTTP or HTTPS");

    zget_options_init(&options);
    failed |= expect_option_error(&options, "URL must use HTTP or HTTPS");

    /* A newer caller may pass a larger append-only structure; ignore its tail. */
    memset(&future, 0, sizeof(future));
    zget_options_init(&future.known);
    future.known.struct_size = sizeof(future);
    future.field_unknown_to_this_library = UINT64_MAX;
    failed |= expect_option_error(&future.known, "URL must use HTTP or HTTPS");

    zget_global_cleanup();
    return failed ? 1 : 0;
}
