# zget

Fetch one file from a remote ZIP archive without downloading the complete archive.

`zget` is a command-line tool and C library for listing and extracting
individual files from remote ZIP and ZIP64 archives using precise HTTP Range
requests. If a server cannot provide the required ranges, zget fails rather
than silently downloading the complete archive. It streams archive metadata
with memory usage independent of entry count, so the same design works for
ordinary archives and scales to very large ones.

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

Listings use the familiar `unzip -l` columns: uncompressed length, modification
date and time in UTC, and member name. Timestamps are resolved from NTFS,
Extended Timestamp, or packed DOS metadata in that order. Listings scan the
Central Directory once and retain only the current entry. Control characters
and backslashes are escaped so every member stays on one safe output line.

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
remains practical as archive sizes and entry counts grow, with a strict and
predictable Range-access contract.

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

A successful lookup uses semantically precise byte ranges:

```text
tail -> central directory -> target local header -> target payload
```

If the server ignores a required Range request or otherwise cannot provide a
valid partial response, zget fails with a Range or HTTP error. It never silently
turns selective member retrieval into a complete archive download.

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
header is `<zget.h>`. The public API consists of two synchronous one-shot
operations, callbacks, member records, and zget error codes; it exposes no
contexts, global lifecycle, or transport handles. libcurl and zlib remain
private implementation dependencies. Current library support is HTTP(S),
single-volume ZIP/ZIP64, STORE, and DEFLATE.

The library is pre-1.0. Its API and ABI may change between minor releases while
the design is refined; patch releases preserve the ABI within one `0.MINOR`
line. Compile-time version macros are available in `<zget_version.h>`, and
`zget_version()` reports the linked library version at runtime.

Link with pkg-config:

```sh
cc -o example example.c $(pkg-config --cflags --libs libzget)
```

Or use the installed CMake target `Zget::libzget` after
`find_package(Zget 0.5 REQUIRED)`.

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
    int rc;

    if (argc != 3) {
        fprintf(stderr, "usage: %s URL MEMBER\n", argv[0]);
        return 2;
    }
    rc = zget_get(argv[1], argv[2], write_stdout, stdout);
    if (rc != ZGET_OK)
        fprintf(stderr, "zget: %s\n", zget_error_string(rc));
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

`zget_list()` streams every Central Directory entry as one borrowed record and
does not construct an archive-wide index:

```c
static int print_member(void *userdata, const zget_member_info *member)
{
    FILE *output = userdata;

    if (fwrite(member->name, 1, member->name_length, output) !=
        member->name_length || fputc('\n', output) == EOF)
        return 1;
    return 0;
}

rc = zget_list(url, print_member, stdout);
```

The record and its NUL-terminated name become invalid when the callback
returns. Every name is valid UTF-8. Resolution uses the ZIP UTF-8 flag first,
then a valid Info-ZIP Unicode Path (`0x7075`) field, then CP437 conversion.
`mtime` is signed Unix time in UTC seconds. The record also supplies compressed
and uncompressed sizes, CRC32, and the numeric ZIP compression method.

Both public operations manage their own resources and libcurl lifecycle. They
are synchronous and callbacks run inline. Independent calls share no archive
state and may be made from different threads on supported libcurl builds.

## HTTP invariants

Range-capable servers must return each accepted archive-data response as a
matching `206 Partial Content` response with the correct body length. The
initial tail normally uses a suffix range. If a server rejects or ignores that
syntax but supports explicit ranges, zget uses a one-byte size probe and retries
the tail as an explicit interval.

If a server ignores the required Range request entirely and returns HTTP 200
with the complete representation, zget rejects the response with
`ZGET_ERANGE`. Supplying `MEMBER` requests selective retrieval; zget never
silently replaces it with a complete archive download.

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
- Names are resolved to valid UTF-8 using the UTF-8 flag, a valid Info-ZIP
  Unicode Path field, or CP437. Embedded NUL bytes are malformed.
- No encryption, split archives, resume, or random seeks within a DEFLATE
  member.
- HTTP Range support is required. Servers that ignore required Range requests
  fail with a Range error instead of triggering a complete download.

This project is MIT licensed.
