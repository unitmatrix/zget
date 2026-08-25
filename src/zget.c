#include "internal.h"

#include "format/format.h"
#include "source/http.h"
#include "source/source.h"
#include "util.h"
#include "zget.h"

#include <string.h>

#define ZGET_DEFAULT_REDIRECTS 8u

static int operation_open(const char *archive_url,
                          struct zget_operation *operation)
{
    struct zget_http_options http_options = {ZGET_DEFAULT_REDIRECTS};
    int rc;

    memset(operation, 0, sizeof(*operation));
    rc = zget_http_source_open(archive_url, &http_options, &operation->error,
                               &operation->source);
    if (rc == ZGET_OK)
        rc = zget_format_open(operation->source, &operation->error,
                              &operation->format);
    return rc;
}

static void operation_close(struct zget_operation *operation)
{
    zget_format_close(operation->format);
    zget_source_close(operation->source);
}

static int member_valid(const char *member)
{
    size_t length;

    if (member == NULL || member[0] == '\0')
        return 0;
    length = strlen(member);
    return zget_valid_utf8((const unsigned char *)member, length);
}

int zget_get(const char *archive_url, const char *member_name,
             zget_write_cb write_cb, void *userdata)
{
    struct zget_operation operation;
    int rc;

    if (archive_url == NULL || archive_url[0] == '\0' ||
        !member_valid(member_name) || write_cb == NULL)
        return ZGET_EINVAL;
    rc = zget_http_global_init();
    if (rc != ZGET_OK)
        return rc;
    rc = operation_open(archive_url, &operation);
    if (rc == ZGET_OK)
        rc = zget_format_extract_member(operation.format, member_name,
                                        write_cb, userdata);
    operation_close(&operation);
    zget_http_global_cleanup();
    return rc;
}

int zget_list(const char *archive_url, zget_list_cb list_cb, void *userdata)
{
    struct zget_operation operation;
    int rc;

    if (archive_url == NULL || archive_url[0] == '\0' || list_cb == NULL)
        return ZGET_EINVAL;
    rc = zget_http_global_init();
    if (rc != ZGET_OK)
        return rc;
    rc = operation_open(archive_url, &operation);
    if (rc == ZGET_OK)
        rc = zget_format_list(operation.format, list_cb, userdata);
    operation_close(&operation);
    zget_http_global_cleanup();
    return rc;
}

const char *zget_error_string(int error)
{
    static const char *const messages[] = {
        "success", "invalid argument", "HTTP error", "HTTP Range unsupported",
        "remote object changed", "malformed ZIP", "unsupported ZIP feature",
        "member not found", "compression error", "CRC32 mismatch",
        "callback rejected data", "out of memory"
    };

    return error >= 0 && (size_t)error < sizeof(messages) / sizeof(messages[0]) ?
           messages[error] : "unknown zget error";
}

const char *zget_version(void)
{
    return ZGET_VERSION_STRING;
}
