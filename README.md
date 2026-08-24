# zget

Fetch one file from a remote ZIP archive while downloading only the bytes needed when the server supports HTTP Range requests.

`zget` is a command-line tool and C library for listing and extracting
individual files from remote ZIP and ZIP64 archives. It prefers precise HTTP
Range requests and transparently falls back to a complete temporary download
when a server ignores Range requests entirely. It streams archive metadata with
memory usage independent of entry count, so the same design works for ordinary
archives and scales to very large ones.

```sh
zget https://example.com/archive.zip README.txt
zget -o report.pdf https://example.com/documents.zip path/to/report.pdf
zget -l https://example.com/archive.zip
zget -l https://example.com/documents.zip path/to/report.pdf
zget -1 https://example.com/archive.zip
zget https://example.com/data.zip config.json | jq .
```

## Installation

Install the required runtime dependencies (`libcurl` and `zlib`) first. Then
download the archive for your platform from the
[latest release](https://github.com/unitmatrix/zget/releases/latest), extract it,
and put `zget` somewhere on your `$PATH`.

Linux binaries target Ubuntu 24.04. macOS binaries require macOS 13.2+.

### Ubuntu 24.04 x86_64

Replace `X.Y.Z` with the release version you want to install.

```sh
sudo apt install -y libcurl4t64 zlib1g
curl -fLO https://github.com/unitmatrix/zget/releases/download/vX.Y.Z/zget-X.Y.Z-linux-x86_64.tar.gz
tar -xzf zget-X.Y.Z-linux-x86_64.tar.gz
sudo install zget-X.Y.Z-linux-x86_64/zget /usr/local/bin/zget
```

## Usage

```text
zget [-o FILE] URL MEMBER
zget -l URL [MEMBER]
zget -1 URL [MEMBER]
```

## Familiar by design

Archive operations follow `unzip` where practical, while output conventions
follow `curl`. `zget URL MEMBER` therefore streams to standard output, while
`-o FILE` streams directly to a local output using normal file-open semantics:
an existing file is truncated and overwritten, and `-o -` selects standard
output.
`-l` requests an `unzip`-style listing; the compact `-1` form follows `zipinfo`
and emits only names.

Extraction requires both a URL and an exact member name. A URL without a member
is a usage error rather than an implicit request to list the archive; use `-l`
explicitly to stream all entries, or add `MEMBER` to list one exact match.

Listings use the familiar `unzip -l` columns: uncompressed length, ZIP-local
modification date and time, and member name. ZIP timestamps have no timezone;
invalid packed timestamps are shown as dashes rather than normalized. Listings
scan the Central Directory once and retain only the current entry. Control
characters, backslashes, and legacy non-UTF-8 name bytes are escaped so every
member stays on one safe output line.

For a compact listing, `-1` writes only member names, one per line, with no
header or totals. It accepts the same optional exact `MEMBER` as `-l` and uses
the same safe escaping.

## Why zget?

Remote ZIP access is not unique to zget. Python projects such as
[`remotezip`](https://github.com/gtsystem/python-remotezip) and
[`unzip-http`](https://github.com/saulpw/unzip-http) already list and extract
members over HTTP without downloading the whole archive when Range requests are
available. `zget` is a native C library and CLI for efficient remote ZIP/ZIP64
access. Its streaming, bounded-memory design works for ordinary archives and
remains practical as archive sizes and entry counts grow, with predictable
Range behavior and a compatibility fallback for servers that ignore Range.

`zget` streams the Central Directory, discards metadata for non-matching entries
immediately, and can stop scanning as soon as the requested entry is found. It
does not build an in-memory index of the entire archive.

```text
Central Directory HTTP stream
         ↓
parse one entry
         ↓
compare exact name
    ├─ no match → discard metadata → next entry
    └─ target found → stop the HTTP transfer
```

Memory use is O(1) with respect to archive entry count. This property matters
for archives containing hundreds of thousands, millions, or tens of millions
of entries; scan time and transferred metadata still depend on the target's
position in the Central Directory.

When the server supports Range requests, a successful lookup uses semantically
precise byte ranges:

```text
tail -> central directory -> target local header -> target payload
```

If the server ignores Range requests entirely and returns the complete object,
zget downloads the archive once into anonymous temporary storage and continues
through the same local source and ZIP implementation. The operation remains
transparent to callers, but it necessarily transfers the whole archive and
therefore loses the bandwidth advantage of Range-based access.

The first exact Central Directory name match wins. Extraction is streamed
through STORE or raw-DEFLATE decoding and checked against the entry CRC32.

## Architecture

The CLI is a thin frontend over `libzget`. Internally, format code requests
semantic byte ranges from a source instead of depending on a transport:

```text
zget CLI
    |
public libzget API
    |
format engine  -- semantic ranges -->  source
    |                                  |
ZIP / zlib                         HTTP / libcurl
```

The HTTP source knows nothing about ZIP layout, and the ZIP engine knows
nothing about libcurl. This small internal boundary exists for clear ownership
and independent testing; it is not a public extension point, registry, or
dynamic plugin system.

## Build

Requirements are CMake 3.16+, libcurl 7.85.0+, and zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake uses installed system copies of libcurl and zlib; production builds do
not vendor or download dependencies. Tests are self-contained and use a local
HTTP fixture rather than the public Internet.

For a staged distro-style install:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR="$PWD/stage" cmake --install build
```

Install destinations follow `GNUInstallDirs`, including multiarch or `lib64`
layouts selected by the toolchain or packager. The install includes the CLI,
shared and static libraries, public header, CMake and pkg-config metadata, the
`zget(1)` man page, and the license under `share/licenses/zget`.

Set `ZGET_ENABLE_SANITIZERS=ON` for ASan and UBSan. Clang users can set
`ZGET_BUILD_FUZZERS=ON` to build the libFuzzer parser targets.
`ZGET_BUILD_LARGE_TESTS=ON` adds a generated 100k-entry integration test; set
`ZGET_MILLION_ENTRY_TEST=1` when running CTest to include one million entries.

The build produces shared and static `libzget` libraries. The `zget` executable
links the project library statically, so it does not require a separately
installed `libzget`; curl, zlib, TLS, and the platform C runtime remain dynamic.

## Library

`libzget` is the reusable implementation behind the `zget` CLI. Its installed
header is `<zget.h>` and its public API deals only in URLs, opaque contexts,
format-neutral member records, callbacks, and zget error codes. libcurl and
zlib remain private implementation dependencies: applications do not include
their headers or manipulate their handles to use the high-level API. Current
library support is HTTP(S), single-volume ZIP/ZIP64, STORE, and DEFLATE.

The library is pre-1.0. Its API and ABI may change between minor releases while
the design is refined; patch releases preserve the ABI within one `0.MINOR`
line. Compile-time version macros are available in `<zget_version.h>`, and
`zget_version()` reports the linked library version at runtime.

Link with pkg-config:

```sh
cc -o example example.c $(pkg-config --cflags --libs libzget)
```

Or use the installed CMake target `Zget::libzget` after
`find_package(Zget 0.4 REQUIRED)`.

### Minimal extraction example

```c
#include <zget.h>

#include <stdio.h>

static int write_stdout(void *userdata, const void *data, size_t size)
{
    FILE *output = userdata;
    return fwrite(data, 1, size, output) == size ? 0 : 1;
}

int main(int argc, char **argv)
{
    zget_ctx *ctx = NULL;
    int rc;

    if (argc != 3) {
        fprintf(stderr, "usage: %s URL MEMBER\n", argv[0]);
        return 2;
    }
    rc = zget_global_init();
    if (rc != ZGET_OK) {
        fprintf(stderr, "zget: %s\n", zget_error_string(rc));
        return 1;
    }

    rc = zget_open_url_ex(argv[1], NULL, &ctx);
    if (rc == ZGET_OK)
        rc = zget_extract_member(ctx, argv[2], write_stdout, stdout);
    if (rc != ZGET_OK) {
        const char *detail = ctx != NULL ? zget_last_error_message(ctx) : "";
        fprintf(stderr, "zget: %s%s%s\n", zget_error_string(rc),
                detail[0] != '\0' ? ": " : "", detail);
    }

    zget_close(ctx);
    zget_global_cleanup();
    return rc == ZGET_OK ? 0 : 1;
}
```

The output callback borrows each buffer only for that callback invocation.
Extraction may emit data before a later decompression, size, or CRC failure, so
only a `ZGET_OK` return guarantees complete validated output. Applications that
need atomic publication should stream to temporary storage and publish it only
after success. The CLI deliberately gives `-o` ordinary curl-style streaming
semantics instead.

### Listing

`zget_list()` streams one borrowed, length-delimited record at a time and does
not construct an archive-wide index. Pass NULL as the member path to list every
entry, or an exact path to emit only the first match:

```c
static int print_member(void *userdata, const zget_member_info *member)
{
    FILE *output = userdata;

    if (fwrite(member->name, 1, member->name_length, output) !=
        member->name_length || fputc('\n', output) == EOF)
        return 1;
    return 0;
}

/* ctx is an open zget_ctx. */
rc = zget_list(ctx, NULL, print_member, stdout);
```

The record and its name become invalid when the callback returns. Validated
modification components are local calendar values because ZIP stores no
timezone. When `ZGET_MEMBER_NAME_UTF8` is absent, the name contains legacy raw
bytes and should be preserved or escaped rather than decoded by guesswork.

### Lifecycle, limits, and threads

Call `zget_global_init()` during single-threaded application startup and match
every successful call with `zget_global_cleanup()` during shutdown after all
contexts are closed. The CLI handles this lifecycle automatically.

`zget_open_url_ex()` retains a diagnostic context on most opening failures;
inspect it with `zget_last_error_message()` and always release it with
`zget_close()`. `zget_open_url()` is shorter when only success or failure is
needed, while `zget_get()` performs one open/extract/close sequence using
default options.

A context must not be used concurrently or reentered from one of its callbacks.
After global initialization, different contexts may be used by different
threads. `max_output_size`, `max_metadata_bytes`, and `max_http_requests` let
embedding applications impose defensive limits. The CLI leaves output and
metadata sizes unrestricted. Always start custom options with
`zget_options_init()`; the library copies the structure while opening.

### Migrating to 0.3

Version 0.3 removes the ZIP-specific `zget_entry`, `zget_find()`, and
`zget_extract()` interface. Replace the old two-step extraction:

```c
zget_find(ctx, path, &entry);
zget_extract(ctx, &entry, write_cb, userdata);
```

with the format-neutral operation:

```c
zget_extract_member(ctx, path, write_cb, userdata);
```

Use `zget_list(ctx, path, list_cb, userdata)` when only member metadata is
needed. This keeps ZIP offsets, flags, compression details, and CRC state inside
the library so those implementation details can evolve safely.

## HTTP invariants

Range-capable servers must return each accepted archive-data response as a
matching `206 Partial Content` response with the correct body length. The
initial tail normally uses a suffix range. If a server rejects or ignores that
syntax but supports explicit ranges, zget uses a one-byte size probe and retries
the tail as an explicit interval. These extra requests count toward
`max_http_requests`.

If a server ignores the required Range request entirely and returns HTTP 200
with the complete representation, zget falls back to one complete download in
anonymous temporary storage and continues through the local-file source. This
fallback preserves extraction and listing behavior, but it transfers the entire
archive and does not provide Range-based bandwidth savings.

An inconsistent `Content-Range`, changed object size, non-identity
`Content-Encoding`, or failed `If-Match` aborts the Range operation. HTTPS
redirects cannot downgrade to HTTP. A strong ETag from the first accepted
response is used for later `If-Match` requests. Without one, consistency is
best-effort and still checks the object size.

`-o FILE` opens the requested path normally and streams member data into it.
Existing regular files are truncated, and paths such as symlinks, FIFOs, and
devices follow the platform's normal open behavior. If extraction fails after
writing begins, the partial output remains. `-o -` and the default output both
write to stdout, which likewise cannot be rolled back after a late error.

## Scope

- Exact, case-sensitive full member paths; no normalization or globbing.
- Single-volume ZIP32 and ZIP64, including data descriptors.
- Compression methods STORE (0) and DEFLATE (8) only.
- UTF-8-flagged names must be valid UTF-8. Legacy names are eligible for exact
  matching only when entirely ASCII; CP437 conversion is intentionally absent.
- No encryption, split archives, resume, or random seeks within a DEFLATE
  member.
- HTTP Range is preferred for efficient remote access. Servers that ignore
  Range entirely are supported through a transparent complete-download fallback.

This project is MIT licensed.
