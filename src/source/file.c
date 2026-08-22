#include "source/file.h"

#include "source/source-private.h"
#include "zget.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct zget_file_source {
    struct zget_source source;
    int fd;
};

static int file_error(struct zget_file_source *file, const char *operation)
{
    int saved_errno = errno;

    zget_error_set(file->source.error, ZGET_EIO, "%s: %s", operation,
                   strerror(saved_errno));
    return ZGET_EIO;
}

static int file_read_range(struct zget_source *source, uint64_t offset,
                           uint64_t length, zget_source_write_cb write_cb,
                           void *userdata)
{
    struct zget_file_source *file = (struct zget_file_source *)source;
    unsigned char buffer[64 * 1024];
    uint64_t position = offset;
    uint64_t remaining = length;

    /*
     * Positional reads keep the source independent of mutable descriptor state,
     * so one range cannot accidentally change where a later range starts.
     */
    while (remaining != 0) {
        size_t want = remaining < sizeof(buffer) ?
                      (size_t)remaining : sizeof(buffer);
        ssize_t got;
        enum zget_source_action action;

        do {
            got = pread(file->fd, buffer, want, (off_t)position);
        } while (got < 0 && errno == EINTR);
        if (got < 0)
            return file_error(file, "pread");
        if (got == 0) {
            zget_error_set(source->error, ZGET_EIO,
                           "unexpected end of local file");
            return ZGET_EIO;
        }

        action = write_cb(userdata, buffer, (size_t)got);
        if (action == ZGET_SOURCE_STOP)
            return ZGET_OK;
        if (action == ZGET_SOURCE_ERROR)
            return source->error != NULL && source->error->code != ZGET_OK ?
                   source->error->code : ZGET_EIO;

        position += (uint64_t)got;
        remaining -= (uint64_t)got;
    }
    return ZGET_OK;
}

static int file_read_suffix(struct zget_source *source, uint64_t length,
                            zget_source_write_cb write_cb, void *userdata)
{
    uint64_t take = length < source->size ? length : source->size;

    if (take == 0)
        return ZGET_EINVAL;
    return file_read_range(source, source->size - take, take, write_cb,
                           userdata);
}

static void file_close(struct zget_source *source)
{
    struct zget_file_source *file = (struct zget_file_source *)source;

    if (file->fd >= 0)
        (void)close(file->fd);
    free(file);
}

static const struct zget_source_ops file_ops = {
    file_read_range,
    file_read_suffix,
    file_close
};

int zget_file_source_open(const char *path, struct zget_error_state *error,
                          struct zget_source **out_source)
{
    struct zget_file_source *file;
    struct stat st;
    int fd;

    if (out_source != NULL)
        *out_source = NULL;
    if (path == NULL || error == NULL || out_source == NULL)
        return ZGET_EINVAL;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        int saved_errno = errno;
        zget_error_set(error, ZGET_EIO, "open local file: %s",
                       strerror(saved_errno));
        return ZGET_EIO;
    }
    if (fstat(fd, &st) != 0) {
        int saved_errno = errno;
        (void)close(fd);
        zget_error_set(error, ZGET_EIO, "stat local file: %s",
                       strerror(saved_errno));
        return ZGET_EIO;
    }
    if (st.st_size <= 0) {
        (void)close(fd);
        zget_error_set(error, ZGET_EINVAL, "local file is empty");
        return ZGET_EINVAL;
    }

    file = calloc(1, sizeof(*file));
    if (file == NULL) {
        (void)close(fd);
        zget_error_set(error, ZGET_ENOMEM, "allocating local file source");
        return ZGET_ENOMEM;
    }

    zget_source_init(&file->source, &file_ops, error);
    file->fd = fd;
    file->source.size = (uint64_t)st.st_size;
    file->source.size_known = true;
    *out_source = &file->source;
    return ZGET_OK;
}
