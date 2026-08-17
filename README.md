# zget

Fetch one file from a remote ZIP archive without downloading the archive.

Designed for very large ZIP/ZIP64 archives, with streaming Central Directory
parsing and memory usage independent of archive entry count.

```sh
zget https://example.com/archive.zip path/to/file.iso
zget -o file.iso https://example.com/archive.zip path/to/file.iso
zget https://example.com/data.zip config.json | jq .
```

## Why zget?

Tools already exist for accessing remote ZIP archives over HTTP. `zget` focuses
on a narrower case: fetching one known member from a very large ZIP/ZIP64
archive with predictable network behavior and bounded memory use.

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

A successful lookup uses semantically precise HTTP Range requests:

```text
tail -> central directory -> target local header -> target payload
```

The first exact Central Directory name match wins. Extraction is streamed
through STORE or raw-DEFLATE decoding and checked against the entry CRC32.

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

The installed header is `<zget.h>`. `zget_open_url_ex()` retains a diagnostic
context even when opening fails; inspect it with `zget_last_error_message()` and
always release it with `zget_close()`. `zget_open_url()` is the shorter API when
only success or failure is needed. A context must not be used concurrently;
different contexts are independent.

`max_output_size`, `max_metadata_bytes`, and `max_http_requests` let embedding
applications impose defensive limits. The CLI leaves output and metadata sizes
unrestricted.

## HTTP invariants

Every request must return one matching `206 Partial Content` response with the
correct body length. A `200` response, inconsistent `Content-Range`, changed
object size, non-identity `Content-Encoding`, or failed `If-Match` aborts the
operation. HTTPS redirects cannot downgrade to HTTP. A strong ETag from the
first response is used for later `If-Match` requests. Without one, consistency
is best-effort and still checks the object size.

`-o` writes a temporary file in the destination directory and publishes it only
after decompression and CRC validation. Existing paths are never overwritten.
Stdout cannot be rolled back if a late error occurs.

## v0.1 scope

- Exact, case-sensitive full member paths; no normalization or globbing.
- Single-volume ZIP32 and ZIP64, including data descriptors.
- Compression methods STORE (0) and DEFLATE (8) only.
- UTF-8-flagged names must be valid UTF-8. Legacy names are eligible for exact
  matching only when entirely ASCII; CP437 conversion is intentionally absent.
- No encryption, split archives, listing UI, resume, or random seeks within a
  DEFLATE member.
- HTTP Range support is mandatory; there is no full-download fallback.

This project is MIT licensed.
