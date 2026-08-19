#include "zget.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct file_output {
    FILE *file;
    int broken_pipe;
};

struct list_output {
    struct file_output stream;
    uint64_t entries;
    uint64_t total_size;
    int total_overflow;
    int names_only;
};

static int write_stream(struct file_output *out, const void *data, size_t size)
{
    errno = 0;
    if (fwrite(data, 1, size, out->file) == size)
        return 0;
    if (errno == EPIPE)
        out->broken_pipe = 1;
    return 1;
}

static int write_file(void *opaque, const void *data, size_t size)
{
    return write_stream(opaque, data, size);
}

static int write_escaped_name(struct list_output *out,
                              const zget_member_info *member)
{
    const unsigned char *name = (const unsigned char *)member->name;
    size_t begin = 0, i;

    for (i = 0; i < member->name_length; ++i) {
        const unsigned char byte = name[i];
        const int raw = byte >= 0x20 && byte != 0x7f && byte != '\\' &&
                        ((member->flags & ZGET_MEMBER_NAME_UTF8) != 0 ||
                         byte < 0x80);
        const char *escape = NULL;
        char hex[4];

        if (raw)
            continue;
        if (i != begin && write_stream(&out->stream, name + begin, i - begin))
            return 1;
        if (byte == '\n')
            escape = "\\n";
        else if (byte == '\r')
            escape = "\\r";
        else if (byte == '\t')
            escape = "\\t";
        else if (byte == '\\')
            escape = "\\\\";
        if (escape != NULL) {
            if (write_stream(&out->stream, escape, 2))
                return 1;
        } else {
            static const char digits[] = "0123456789abcdef";
            hex[0] = '\\'; hex[1] = 'x';
            hex[2] = digits[byte >> 4]; hex[3] = digits[byte & 0x0f];
            if (write_stream(&out->stream, hex, sizeof(hex)))
                return 1;
        }
        begin = i + 1;
    }
    return begin != member->name_length &&
           write_stream(&out->stream, name + begin,
                        member->name_length - begin);
}

static int list_member(void *opaque, const zget_member_info *member)
{
    struct list_output *out = opaque;
    char prefix[64];
    int length;

    /*
     * The short form deliberately shares the normal name encoder. Besides
     * making one-name-per-line output pleasant for scripts, this prevents a
     * crafted archive name from injecting additional terminal lines.
     */
    if (out->names_only)
        return write_escaped_name(out, member) ||
               write_stream(&out->stream, "\n", 1);

    if ((member->flags & ZGET_MEMBER_HAS_MODIFIED_TIME) != 0)
        length = snprintf(prefix, sizeof(prefix),
                          "%9" PRIu64 "  %02u-%02u-%04u %02u:%02u   ",
                          member->uncompressed_size,
                          (unsigned int)member->modified_month,
                          (unsigned int)member->modified_day,
                          (unsigned int)member->modified_year,
                          (unsigned int)member->modified_hour,
                          (unsigned int)member->modified_minute);
    else
        length = snprintf(prefix, sizeof(prefix),
                          "%9" PRIu64 "  ---------- -----   ",
                          member->uncompressed_size);
    if (length < 0 || (size_t)length >= sizeof(prefix) ||
        write_stream(&out->stream, prefix, (size_t)length) ||
        write_escaped_name(out, member) ||
        write_stream(&out->stream, "\n", 1))
        return 1;
    ++out->entries;
    if (UINT64_MAX - out->total_size < member->uncompressed_size)
        out->total_overflow = 1;
    else if (!out->total_overflow)
        out->total_size += member->uncompressed_size;
    return 0;
}

static void usage(FILE *file)
{
    fprintf(file, "usage: zget [-o FILE] URL MEMBER\n"
                  "       zget -l URL [MEMBER]\n"
                  "       zget -1 URL [MEMBER]\n");
}

static char *temporary_name(const char *path)
{
    /* Same-directory placement keeps final publication on one filesystem. */
    const char *slash = strrchr(path, '/');
    const char *base = slash == NULL ? path : slash + 1;
    size_t dir_len = slash == NULL ? 0 : (size_t)(slash - path + 1);
    size_t n = dir_len + 1 + strlen(base) + sizeof(".zget.tmp.XXXXXX");
    char *name = malloc(n);
    if (name == NULL)
        return NULL;
    if (dir_len != 0)
        memcpy(name, path, dir_len);
    (void)snprintf(name + dir_len, n - dir_len, ".%s.zget.tmp.XXXXXX", base);
    return name;
}

int main(int argc, char **argv)
{
    const char *output_path = NULL, *url, *member = NULL;
    char *temp_path = NULL;
    struct stat st;
    struct file_output output = {stdout, 0};
    struct list_output listing = {{stdout, 0}, 0, 0, 0, 0};
    zget_ctx *ctx = NULL;
    zget_options options;
    int arg = 1, fd = -1, list_mode = 0, rc, exit_status = 1;

    if (argc == 2 && !strcmp(argv[1], "--version")) {
        printf("zget %s\n", zget_version());
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        return 0;
    }
    if (arg < argc && (!strcmp(argv[arg], "-l") ||
                       !strcmp(argv[arg], "-1"))) {
        list_mode = 1;
        listing.names_only = !strcmp(argv[arg], "-1");
        ++arg;
    } else if (arg < argc && !strcmp(argv[arg], "-o")) {
        if (++arg >= argc || argv[arg][0] == '\0') {
            usage(stderr);
            return 2;
        }
        output_path = argv[arg++];
    }
    if ((list_mode && (argc - arg < 1 || argc - arg > 2)) ||
        (!list_mode && argc - arg != 2)) {
        usage(stderr);
        return 2;
    }
    url = argv[arg];
    if (!list_mode || argc - arg == 2)
        member = argv[arg + 1];
    if (member != NULL && member[0] == '\0') {
        fprintf(stderr, "zget: member path must not be empty\n");
        return 2;
    }

#ifdef SIGPIPE
    /*
     * Shell pipelines expect a writer to terminate quietly when its reader
     * exits. Reset an inherited ignored disposition so fwrite cannot turn that
     * ordinary condition into a noisy libzget output error.
     */
    if (signal(SIGPIPE, SIG_DFL) == SIG_ERR) {
        fprintf(stderr, "zget: cannot configure SIGPIPE: %s\n", strerror(errno));
        return 1;
    }
#endif

    /* The CLI owns one process-wide acquisition around all network work. */
    rc = zget_global_init();
    if (rc != ZGET_OK) {
        fprintf(stderr, "zget: %s\n", zget_error_string(rc));
        return 1;
    }
    zget_options_init(&options);
    /* CLI users may legitimately target archives with enormous directories. */
    options.max_metadata_bytes = UINT64_MAX;
    rc = zget_open_url_ex(url, &options, &ctx);
    if (rc != ZGET_OK)
        goto zget_failure;

    if (list_mode) {
        static const char header[] =
            "  Length      Date    Time    Name\n"
            "---------  ---------- -----   ----\n";
        static const char footer[] =
            "---------                     -------\n";
        char summary[96];
        int length;

        /* Names-only output has no decoration, matching zipinfo -1. */
        if (!listing.names_only &&
            write_stream(&listing.stream, header, sizeof(header) - 1))
            goto done;
        rc = zget_list(ctx, member, list_member, &listing);
        if (rc != ZGET_OK) {
            if (listing.stream.broken_pipe)
                goto done;
            goto zget_failure;
        }
        if (!listing.names_only) {
            if (write_stream(&listing.stream, footer, sizeof(footer) - 1))
                goto done;
            if (listing.total_overflow)
                length = snprintf(summary, sizeof(summary),
                                  "%9s                     %" PRIu64 " %s\n",
                                  "overflow", listing.entries,
                                  listing.entries == 1 ? "file" : "files");
            else
                length = snprintf(
                    summary, sizeof(summary),
                    "%9" PRIu64 "                     %" PRIu64 " %s\n",
                    listing.total_size, listing.entries,
                    listing.entries == 1 ? "file" : "files");
            if (length < 0 || (size_t)length >= sizeof(summary) ||
                write_stream(&listing.stream, summary, (size_t)length))
                goto done;
        }
        if (fflush(listing.stream.file) != 0) {
            if (errno == EPIPE)
                goto done;
            fprintf(stderr, "zget: cannot flush output: %s\n", strerror(errno));
            goto done;
        }
        exit_status = 0;
        goto done;
    }

    if (output_path != NULL) {
        /*
         * Never stream into the destination itself: decompression or CRC may
         * fail after substantial output. mkstemp gives this invocation unique,
         * private scratch storage which every failure path removes below.
         */
        if (lstat(output_path, &st) == 0) {
            fprintf(stderr, "zget: output already exists: %s\n", output_path);
            goto done;
        }
        if (errno != ENOENT) {
            fprintf(stderr, "zget: cannot inspect output: %s\n", strerror(errno));
            goto done;
        }
        temp_path = temporary_name(output_path);
        if (temp_path == NULL || (fd = mkstemp(temp_path)) < 0) {
            fprintf(stderr, "zget: cannot create temporary output: %s\n", strerror(errno));
            goto done;
        }
        output.file = fdopen(fd, "wb");
        if (output.file == NULL) {
            fprintf(stderr, "zget: cannot open temporary output: %s\n", strerror(errno));
            close(fd);
            fd = -1;
            goto done;
        }
        fd = -1;
    }

    /* Keep ZIP-specific lookup details behind libzget's format-neutral API. */
    rc = zget_extract_member(ctx, member, write_file, &output);
    if (rc != ZGET_OK) {
        if (output.broken_pipe)
            goto done;
        goto zget_failure;
    }
    /* Publish only after library validation and durable file contents succeed. */
    if (fflush(output.file) != 0 || (output_path != NULL && fsync(fileno(output.file)) != 0)) {
        if (errno == EPIPE)
            goto done;
        fprintf(stderr, "zget: cannot flush output: %s\n", strerror(errno));
        goto done;
    }
    if (output_path != NULL) {
        if (fclose(output.file) != 0) {
            output.file = NULL;
            fprintf(stderr, "zget: cannot close output: %s\n", strerror(errno));
            goto done;
        }
        output.file = NULL;
        /* link(2) publishes atomically and cannot overwrite an existing path. */
        if (link(temp_path, output_path) != 0) {
            fprintf(stderr, "zget: cannot publish output: %s\n", strerror(errno));
            goto done;
        }
        (void)unlink(temp_path);
    }
    exit_status = 0;
    goto done;

zget_failure:
    fprintf(stderr, "zget: %s%s%s\n", zget_error_string(rc),
            ctx != NULL && zget_last_error_message(ctx)[0] != '\0' ? ": " : "",
            ctx != NULL ? zget_last_error_message(ctx) : "");
done:
    /* stdout is borrowed; only a stream opened for -o is owned and closed here. */
    if (output_path != NULL && output.file != NULL)
        (void)fclose(output.file);
    if (fd >= 0)
        (void)close(fd);
    if (temp_path != NULL) {
        (void)unlink(temp_path);
        free(temp_path);
    }
    zget_close(ctx);
    zget_global_cleanup();
    return exit_status;
}
