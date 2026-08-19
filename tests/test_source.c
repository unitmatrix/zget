#include "source/source-private.h"
#include "zget.h"

#include <stdio.h>

struct fake_source {
    struct zget_source source;
    unsigned int range_reads;
    unsigned int suffix_reads;
    unsigned int closes;
};

static int fake_read_range(struct zget_source *source, uint64_t offset,
                           uint64_t length, zget_source_write_cb write_cb,
                           void *userdata)
{
    struct fake_source *fake = (struct fake_source *)source;

    (void)offset;
    (void)length;
    (void)write_cb;
    (void)userdata;
    ++fake->range_reads;
    return ZGET_OK;
}

static int fake_read_suffix(struct zget_source *source, uint64_t length,
                            zget_source_write_cb write_cb, void *userdata)
{
    struct fake_source *fake = (struct fake_source *)source;

    (void)length;
    (void)write_cb;
    (void)userdata;
    ++fake->suffix_reads;
    return ZGET_OK;
}

static void fake_close(struct zget_source *source)
{
    struct fake_source *fake = (struct fake_source *)source;
    ++fake->closes;
}

static enum zget_source_action discard(void *userdata, const void *data,
                                       size_t size)
{
    (void)userdata;
    (void)data;
    (void)size;
    return ZGET_SOURCE_CONTINUE;
}

int main(void)
{
    static const struct zget_source_ops ops = {
        .read_range = fake_read_range,
        .read_suffix = fake_read_suffix,
        .close = fake_close
    };
    struct zget_error_state error = {0};
    struct fake_source fake = {0};
    uint64_t size = 0;
    int rc;
    int failed = 0;

    zget_source_init(&fake.source, &ops, &error);
    if (zget_source_get_size(&fake.source, &size)) {
        fprintf(stderr, "new source unexpectedly has a known size\n");
        failed = 1;
    }

    /* Backends publish size only after they have validated source identity. */
    fake.source.size = 100;
    fake.source.size_known = true;
    if (!zget_source_get_size(&fake.source, &size) || size != 100)
        failed = 1;

    if (zget_source_read_range(&fake.source, 90, 10, discard, NULL) != ZGET_OK ||
        fake.range_reads != 1)
        failed = 1;

    /* Invalid intervals must never reach a transport backend. */
    rc = zget_source_read_range(&fake.source, 90, 11, discard, NULL);
    if (rc != ZGET_EINVAL || fake.range_reads != 1 ||
        error.code != ZGET_EINVAL)
        failed = 1;

    if (zget_source_read_suffix(&fake.source, 20, discard, NULL) != ZGET_OK ||
        fake.suffix_reads != 1)
        failed = 1;

    zget_source_close(&fake.source);
    if (fake.closes != 1)
        failed = 1;
    return failed;
}
