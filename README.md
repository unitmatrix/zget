# zget

Fetch one file from a remote ZIP archive without downloading the complete archive.

`zget` is a command-line tool and C library for selective access to remote ZIP
and ZIP64 archives over HTTP Range requests. It can extract one exact member or
stream the archive listing. If the server cannot provide the required ranges,
zget fails rather than silently downloading the complete archive.

```sh
zget https://example.com/archive.zip README.txt
zget -o report.pdf https://example.com/documents.zip path/to/report.pdf
zget https://example.com/data.zip config.json | jq .
zget -l https://example.com/archive.zip
zget -1 https://example.com/archive.zip
```

## Installation

Install the runtime dependencies (`libcurl` and `zlib`), then download the
archive for your platform from the
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
zget -l URL
zget -1 URL
```

`zget URL MEMBER` streams the exact member to standard output. `-o FILE` writes
it directly to the requested path using normal file-open semantics; `-o -`
selects standard output. `-l` prints an `unzip`-style whole-archive listing and
`-1` prints only member names.

Extraction requires both a URL and an exact, case-sensitive member path. A URL
without a member is a usage error; use `-l` or `-1` explicitly to list the
archive.

## Library

`libzget` exposes two synchronous one-shot operations:

```c
int zget_get(const char *archive_url, const char *member_name,
             zget_write_cb write_cb, void *userdata);

int zget_list(const char *archive_url, zget_list_cb list_cb,
              void *userdata);
```

The CLI is the reference consumer of this same public API. Its extraction path
uses the public call directly:

```c
static int write_file(void *opaque, const void *data, size_t size)
{
    return write_stream(opaque, data, size);
}

rc = zget_get(url, member, write_file, &output);
```

Listing follows the same pattern:

```c
rc = zget_list(url, list_member, &listing);
```

Callbacks run inline. Output buffers and listing records are borrowed only for
the duration of the callback. Extraction may emit data before a later
compression, size, or CRC failure, so only `ZGET_OK` guarantees complete
validated output. Applications that require atomic publication should write to
temporary storage and publish it only after success.

Each `zget_member_info` contains a valid UTF-8 name, compressed and uncompressed
sizes, CRC32, numeric ZIP compression method, and modification time as Unix UTC
seconds. Names are resolved from the ZIP UTF-8 flag, a usable Info-ZIP Unicode
Path field, or CP437 in that order.

Link with pkg-config:

```sh
cc -o example example.c $(pkg-config --cflags --libs libzget)
```

Or use the installed CMake target `Zget::libzget` after
`find_package(Zget 0.6 REQUIRED)`.

The library is pre-1.0. API and ABI may change between minor releases; patch
releases preserve the ABI of their `0.MINOR` line. Compile-time version macros
are available in `<zget_version.h>`, and `zget_version()` reports the linked
library version at runtime.

## Why zget?

Remote ZIP access exists in other ecosystems, including Python projects such as
[`remotezip`](https://github.com/gtsystem/python-remotezip) and
[`unzip-http`](https://github.com/saulpw/unzip-http). zget provides the same core
capability as a small native C library and CLI with a strict selective-access
contract.

For exact lookup, zget streams the Central Directory one entry at a time,
discards non-matching metadata immediately, and can stop the HTTP transfer on
the first matching entry. It does not build an archive-wide in-memory index.

```text
archive tail
    ↓
Central Directory stream
    ↓
first exact name match
    ↓
target local header
    ↓
target payload
```

Memory use is O(1) with respect to archive entry count. Scan time and transferred
Central Directory metadata still depend on where the requested member appears.
The payload is streamed through STORE or raw-DEFLATE decoding and checked against
its expected size and CRC32.

## Architecture

The CLI is a thin frontend over `libzget`. Internally, ZIP code requests byte
ranges from a private source abstraction rather than depending on libcurl
itself:

```text
zget CLI
    |
public libzget API
    |
format engine  -- ranges -->  source
    |                         |
ZIP / zlib                HTTP / libcurl
```

The HTTP source knows nothing about ZIP layout, and the ZIP engine knows nothing
about libcurl. This boundary exists for ownership, testing, and fuzzing; it is
not a public plugin or transport API.

## HTTP contract

Archive data is accepted only from valid partial responses. A successful Range
request must return `206 Partial Content` with a matching `Content-Range`, the
expected body length, and the identity representation.

The initial archive tail normally uses a suffix range. Some servers reject
suffix syntax while accepting explicit ranges; in that case zget performs a
one-byte explicit probe to learn the object size and retries the equivalent tail
interval.

A server that ignores a required Range request and returns HTTP 200 is rejected.
Object size must remain stable between requests, and a strong ETag from the
first accepted response is reused with `If-Match` when available. HTTPS
redirects cannot downgrade to HTTP.

## Scope

- HTTP(S) remote sources.
- Single-volume ZIP32 and ZIP64, including data descriptors.
- Exact, case-sensitive member paths; no path or Unicode normalization and no
  globbing.
- Compression methods STORE (0) and DEFLATE (8).
- Valid UTF-8 semantic names resolved from UTF-8, Info-ZIP Unicode Path, or
  CP437 metadata.
- No encryption, split archives, resume, or random seeks within a DEFLATE
  member.
- HTTP Range support is required; there is no complete-download fallback.

## Build

Requirements are CMake 3.16+, libcurl 7.85.0+, and zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake uses installed system copies of libcurl and zlib. Tests use local fixtures
rather than the public Internet. Optional build switches include
`ZGET_ENABLE_SANITIZERS=ON`, `ZGET_BUILD_FUZZERS=ON`, and
`ZGET_BUILD_LARGE_TESTS=ON`; setting `ZGET_MILLION_ENTRY_TEST=1` while running
CTest enables the generated one-million-entry case.

For a staged distro-style install:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR="$PWD/stage" cmake --install build
```

Installation follows `GNUInstallDirs` and includes the CLI, shared and static
libraries, public headers, CMake and pkg-config metadata, the `zget(1)` man page,
and the license. Release CLI binaries link project code statically; curl, zlib,
TLS, and the platform C runtime remain dynamic dependencies.

This project is MIT licensed.
