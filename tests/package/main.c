#include <zget.h>
#include <stdio.h>

static int discard(void *userdata, const void *data, size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return 0;
}

/* Prove an installed consumer can link and call the public library API. */
int main(void)
{
    int failed = 0;

    if (zget_global_init() != ZGET_OK)
        return 1;
    /* Referencing the new symbol also verifies package export visibility. */
    if (zget_extract_member(NULL, "member", discard, NULL) != ZGET_EINVAL)
        failed = 1;
    else
        puts(zget_version());
    zget_global_cleanup();
    return failed;
}
