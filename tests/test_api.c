#include "internal.h"

#include <stdio.h>
#include <string.h>

static int discard(void *userdata, const void *data, size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return 0;
}

static int discard_member(void *userdata, const zget_member_info *member)
{
    (void)userdata;
    (void)member;
    return 0;
}

static int expect_error_values(void)
{
    if (ZGET_OK != 0 || ZGET_EINVAL != 1 || ZGET_EHTTP != 2 ||
        ZGET_ERANGE != 3 || ZGET_ECHANGED != 4 || ZGET_EZIP != 5 ||
        ZGET_EUNSUPPORTED != 6 || ZGET_ENOTFOUND != 7 ||
        ZGET_ECOMPRESSION != 8 || ZGET_ECRC != 9 || ZGET_EDEFLATE != 10 ||
        ZGET_EIO != 11 || ZGET_ELIMIT != 12 || ZGET_ENOMEM != 13 ||
        ZGET_ENOTINITIALIZED != 14) {
        fprintf(stderr, "public error values changed\n");
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

static int expect_invalid_call_updates_error(void)
{
    struct zget_ctx ctx = {0};

    ctx.ready = true;
    ctx.error.code = ZGET_EHTTP;
    strcpy(ctx.error.message, "stale error");
    if (zget_extract_member(&ctx, NULL, discard, NULL) != ZGET_EINVAL ||
        zget_last_error(&ctx) != ZGET_EINVAL ||
        strcmp(zget_last_error_message(&ctx),
               "member path and write callback are required") != 0) {
        fprintf(stderr, "invalid extraction left stale context error state\n");
        return 1;
    }
    if (zget_list(&ctx, NULL, NULL, NULL) != ZGET_EINVAL ||
        zget_last_error(&ctx) != ZGET_EINVAL ||
        strcmp(zget_last_error_message(&ctx),
               "listing callback is required") != 0) {
        fprintf(stderr, "invalid listing left stale context error state\n");
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
    failed |= expect_error_values();
    failed |= expect_invalid_call_updates_error();
    if (zget_extract_member(NULL, "member", discard, NULL) != ZGET_EINVAL) {
        fprintf(stderr, "extract-member accepted a NULL context\n");
        failed = 1;
    }
    if (zget_list(NULL, NULL, discard_member, NULL) != ZGET_EINVAL) {
        fprintf(stderr, "list accepted a NULL context\n");
        failed = 1;
    }
    return failed ? 1 : 0;
}
