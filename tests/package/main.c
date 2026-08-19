#include <zget.h>
#include <stdio.h>

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

/* Prove an installed consumer can link and call the public library API. */
int main(void)
{
    int failed = 0;

    if (zget_global_init() != ZGET_OK)
        return 1;
    /* Referencing the new symbol also verifies package export visibility. */
    if (zget_extract_member(NULL, "member", discard, NULL) != ZGET_EINVAL)
        failed = 1;
    else if (zget_list(NULL, NULL, discard_member, NULL) != ZGET_EINVAL)
        failed = 1;
    else
        puts(zget_version());
    zget_global_cleanup();
    return failed;
}
