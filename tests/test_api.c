#include <zget.h>

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

int main(void)
{
    static const char invalid_utf8[] = {'b', (char)0x80, '\0'};
    int failed = 0;

    if (ZGET_OK != 0 || ZGET_EINVAL != 1 || ZGET_EHTTP != 2 ||
        ZGET_ERANGE != 3 || ZGET_ECHANGED != 4 || ZGET_EZIP != 5 ||
        ZGET_EUNSUPPORTED != 6 || ZGET_ENOTFOUND != 7 ||
        ZGET_ECOMPRESSION != 8 || ZGET_ECRC != 9 ||
        ZGET_ECALLBACK != 10 || ZGET_ENOMEM != 11) {
        fprintf(stderr, "public error values changed\n");
        failed = 1;
    }
    if (zget_get(NULL, "member", discard, NULL) != ZGET_EINVAL ||
        zget_get("", "member", discard, NULL) != ZGET_EINVAL ||
        zget_get("url", NULL, discard, NULL) != ZGET_EINVAL ||
        zget_get("url", "", discard, NULL) != ZGET_EINVAL ||
        zget_get("url", invalid_utf8, discard, NULL) != ZGET_EINVAL ||
        zget_get("url", "member", NULL, NULL) != ZGET_EINVAL) {
        fprintf(stderr, "zget_get accepted invalid input\n");
        failed = 1;
    }
    if (zget_list(NULL, discard_member, NULL) != ZGET_EINVAL ||
        zget_list("", discard_member, NULL) != ZGET_EINVAL ||
        zget_list("url", NULL, NULL) != ZGET_EINVAL) {
        fprintf(stderr, "zget_list accepted invalid input\n");
        failed = 1;
    }
    if (strcmp(zget_error_string(ZGET_ECALLBACK),
               "callback rejected data") != 0 ||
        strcmp(zget_error_string(-1), "unknown zget error") != 0 ||
        strcmp(zget_error_string(999), "unknown zget error") != 0 ||
        zget_version() == NULL || zget_version()[0] == '\0') {
        fprintf(stderr, "error strings or version are invalid\n");
        failed = 1;
    }
    return failed;
}
