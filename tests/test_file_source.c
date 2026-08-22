#include "source/file.h"
#include "source/source.h"
#include "zget.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct buffer {
    unsigned char data[32];
    size_t length;
};

static enum zget_source_action collect(void *userdata, const void *data,
                                       size_t size)
{
    struct buffer *buffer = userdata;

    if (size > sizeof(buffer->data) - buffer->length)
        return ZGET_SOURCE_ERROR;
    memcpy(buffer->data + buffer->length, data, size);
    buffer->length += size;
    return ZGET_SOURCE_CONTINUE;
}

int main(void)
{
    static const char payload[] = "0123456789abcdef";
    char path[] = "/tmp/zget-file-source-XXXXXX";
    struct zget_error_state error = {0};
    struct zget_source *source = NULL;
    struct buffer output = {0};
    uint64_t size = 0;
    int fd = mkstemp(path);
    int rc;

    if (fd < 0)
        return 1;
    if (write(fd, payload, sizeof(payload) - 1) != (ssize_t)(sizeof(payload) - 1)) {
        close(fd);
        unlink(path);
        return 1;
    }
    close(fd);

    rc = zget_file_source_open(path, &error, &source);
    if (rc != ZGET_OK || source == NULL ||
        !zget_source_get_size(source, &size) || size != sizeof(payload) - 1)
        goto fail;

    rc = zget_source_read_range(source, 4, 6, collect, &output);
    if (rc != ZGET_OK || output.length != 6 ||
        memcmp(output.data, "456789", 6) != 0)
        goto fail;

    memset(&output, 0, sizeof(output));
    rc = zget_source_read_suffix(source, 4, collect, &output);
    if (rc != ZGET_OK || output.length != 4 ||
        memcmp(output.data, "cdef", 4) != 0)
        goto fail;

    rc = zget_source_read_range(source, size - 1, 2, collect, &output);
    if (rc != ZGET_EINVAL)
        goto fail;

    zget_source_close(source);
    unlink(path);
    return 0;

fail:
    fprintf(stderr, "file source test failed: rc=%d, error=%s\n",
            rc, error.message);
    zget_source_close(source);
    unlink(path);
    return 1;
}
