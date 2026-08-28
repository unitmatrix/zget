#include "zget.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
    int header_written;
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
        const int raw = byte >= 0x20 && byte != 0x7f && byte != '\\';
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

static int write_list_header(struct list_output *out)
{
    static const char header[] =
        "  Length      Date    Time    Name\n"
        "---------  ---------- -----   ----\n";

    if (out->header_written)
        return 0;
    out->header_written = 1;
    return write_stream(&out->stream, header, sizeof(header) - 1);
}

static int format_utc_mtime(int64_t mtime, char *buffer, size_t size)
{
    time_t timestamp = (time_t)mtime;
    struct tm time;

#ifdef _WIN32
    if (gmtime_s(&time, &timestamp) != 0)
        return 0;
#else
    if (gmtime_r(&timestamp, &time) == NULL)
        return 0;
#endif
    return strftime(buffer, size, "%m-%d-%Y %H:%M", &time) != 0;
}

static int list_member(void *opaque, const zget_member_info *member)
{
    struct list_output *out = opaque;
    char datetime[32];
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

    if (write_list_header(out))
        return 1;
    if (format_utc_mtime(member->mtime, datetime, sizeof(datetime)))
        length = snprintf(prefix, sizeof(prefix),
                          "%9" PRIu64 "  %s   ",
                          member->uncompressed_size, datetime);
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
                  "       zget -l URL\n"
                  "       zget -1 URL\n");
}

int main(int argc, char **argv)
{
    const char *output_path = NULL, *url, *member = NULL;
    struct file_output output = {stdout, 0};
    struct list_output listing = {{stdout, 0}, 0, 0, 0, 0, 0};
    int arg = 1, close_output = 0, list_mode = 0, rc, exit_status = 1;

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
    if ((list_mode && argc - arg != 1) ||
        (!list_mode && argc - arg != 2)) {
        usage(stderr);
        return 2;
    }
    url = argv[arg];
    if (!list_mode)
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

    if (list_mode) {
        static const char footer[] =
            "---------                     -------\n";
        char summary[96];
        int length;

        rc = zget_list(url, list_member, &listing);
        if (rc != ZGET_OK) {
            if (listing.stream.broken_pipe)
                goto done;
            goto zget_failure;
        }
        if (!listing.names_only) {
            if (write_list_header(&listing) ||
                write_stream(&listing.stream, footer, sizeof(footer) - 1))
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

    if (output_path != NULL && strcmp(output_path, "-") != 0) {
        output.file = fopen(output_path, "wb");
        if (output.file == NULL) {
            fprintf(stderr, "zget: cannot open output: %s\n", strerror(errno));
            goto done;
        }
        close_output = 1;
    }

    rc = zget_get(url, member, write_file, &output);
    if (rc != ZGET_OK) {
        if (output.broken_pipe)
            goto done;
        goto zget_failure;
    }
    if (fflush(output.file) != 0) {
        if (errno == EPIPE)
            goto done;
        fprintf(stderr, "zget: cannot flush output: %s\n", strerror(errno));
        goto done;
    }
    if (close_output) {
        if (fclose(output.file) != 0) {
            output.file = NULL;
            fprintf(stderr, "zget: cannot close output: %s\n", strerror(errno));
            goto done;
        }
        output.file = NULL;
        close_output = 0;
    }
    exit_status = 0;
    goto done;

zget_failure:
    fprintf(stderr, "zget: %s\n", zget_error_string(rc));
done:
    /* stdout is borrowed; only a stream opened for named output is owned. */
    if (close_output && output.file != NULL)
        (void)fclose(output.file);
    return exit_status;
}
