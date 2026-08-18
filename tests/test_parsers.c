#include "internal.h"
#include "zip/zip_parse.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
    ++failures; } } while (0)

/* Encode a 16-bit wire value without depending on host endian. */
static void put16(unsigned char *p, uint16_t v)
{ p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
/* Encode a 32-bit wire value from the endian-independent 16-bit helper. */
static void put32(unsigned char *p, uint32_t v)
{ put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
/* Encode a 64-bit wire value from the endian-independent 32-bit helper. */
static void put64(unsigned char *p, uint64_t v)
{ put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

/* Verify checked addition and half-open range containment at their boundaries. */
static void test_arithmetic(void)
{
    uint64_t out = 0;
    /* Remote offsets are valid only if both addition and containment succeed. */
    CHECK(zget_u64_add(1, 2, &out) && out == 3);
    CHECK(!zget_u64_add(UINT64_MAX, 1, &out));
    CHECK(zget_range_valid(9, 1, 10));
    CHECK(!zget_range_valid(9, 2, 10));
}

/* Verify minimal EOCD decoding and rejection of a corrupt signature. */
static void test_eocd(void)
{
    unsigned char e[22] = {0};
    struct zget_eocd out;
    /* A minimal EOCD places one 46-byte Central Directory entry at offset 100. */
    put32(e, 0x06054b50u); put16(e + 8, 1); put16(e + 10, 1);
    put32(e + 12, 46); put32(e + 16, 100);
    CHECK(zget_zip_parse_eocd(e, sizeof(e), 146, 168, &out) == ZGET_OK);
    CHECK(out.cd_offset == 100 && out.cd_size == 46 && out.entries == 1);
    e[0] = 0;
    CHECK(zget_zip_parse_eocd(e, sizeof(e), 146, 168, &out) == ZGET_EZIP);
}

/* Verify ordered ZIP64 extra resolution and rejection of truncated values. */
static void test_zip64_extra(void)
{
    unsigned char e[28] = {0};
    uint64_t size, compressed, offset;
    uint32_t disk;
    /* Saturated ZIP32 fields require the three ZIP64 values in this exact order. */
    put16(e, 1); put16(e + 2, 24);
    put64(e + 4, 7); put64(e + 12, 5); put64(e + 20, 123);
    CHECK(zget_zip_parse_zip64_extra(e, sizeof(e), UINT32_MAX, UINT32_MAX,
          UINT32_MAX, 0, &size, &compressed, &offset, &disk) == ZGET_OK);
    CHECK(size == 7 && compressed == 5 && offset == 123 && disk == 0);
    /* Truncation must fail rather than reinterpret a missing value as another field. */
    CHECK(zget_zip_parse_zip64_extra(e, 8, UINT32_MAX, UINT32_MAX,
          UINT32_MAX, 0, &size, &compressed, &offset, &disk) == ZGET_EZIP);
}

/* Verify acceptance of valid UTF-8 and rejection of an invalid continuation. */
static void test_utf8(void)
{
    static const unsigned char good[] = "a/\xe2\x82\xac";
    static const unsigned char bad[] = {0xe2, 0x28, 0xa1};
    CHECK(zget_valid_utf8(good, sizeof(good) - 1));
    CHECK(!zget_valid_utf8(bad, sizeof(bad)));
}

/* Verify strict Content-Range grammar acceptance and rejection of near-misses. */
static void test_content_range(void)
{
    uint64_t start, end, total;
    CHECK(zget_parse_content_range("bytes 0-99/100", &start, &end, &total));
    CHECK(start == 0 && end == 99 && total == 100);
    /* A leading sign is valid strtoumax input but not valid HTTP grammar. */
    CHECK(!zget_parse_content_range("bytes +0-99/100", &start, &end, &total));
    /* Trailing bytes after a well-formed field must not be silently ignored. */
    CHECK(!zget_parse_content_range("bytes 0-99/100 ", &start, &end, &total));
    CHECK(!zget_parse_content_range("bytes 0-99/", &start, &end, &total));
    CHECK(!zget_parse_content_range("", &start, &end, &total));
}

/* Verify Central Directory parsing across every callback chunk granularity. */
static void test_streaming_cd(void)
{
    unsigned char cd[52] = {0};
    size_t chunk;
    put32(cd, 0x02014b50u);
    put16(cd + 28, 6);
    memcpy(cd + 46, "target", 6);
    /*
     * Every fixed chunk size must find the target across arbitrary splits. The
     * -1 result is the parser's successful "match found; stop transfer" sentinel.
     */
    for (chunk = 1; chunk <= sizeof(cd); ++chunk)
        CHECK(zget_zip_fuzz_cd_stream(cd, sizeof(cd), chunk) == -1);
}

/* Run all parser checks and report aggregate failure through the process status. */
int main(void)
{
    test_arithmetic(); test_eocd(); test_zip64_extra(); test_utf8();
    test_content_range();
    test_streaming_cd();
    return failures != 0;
}
