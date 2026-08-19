# Contributing

Keep changes focused on fetching one exact ZIP member. Core ZIP parsing must
remain independent of HTTP, streaming, hostile-input safe, and O(1) in entry
count. New codecs or archive formats require an explicit product decision.

## Internal architecture

The library joins two independent internal abstractions in `src/zget.c`:

- `src/source/` owns range-addressable byte access, source identity, and
  transport policy. Backends must not interpret container metadata.
- `src/format/` owns container discovery, member lookup, and extraction.
  Engines borrow their source and shared error state from `zget_ctx`; they must
  not close either one.

ZIP-specific state and code live under `src/format/zip/`, including EOCD and
ZIP64 handling, streaming Central Directory parsing, Local Headers, STORE,
DEFLATE, and CRC validation. Adding another format may extend selection inside
the format layer, but must not require changes to an HTTP backend. Likewise,
adding another source must not require changes to ZIP parsing.

The format layer requests semantic exact or suffix ranges rather than treating
a source as a seekable file. Preserve streaming callbacks, early-stop behavior,
and memory use independent of archive entry count; do not add a generic cache
or archive-wide index without a measured reason.

Before submitting a change, build with both GCC and Clang where available and
run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-sanitize -DZGET_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Use small functions, checked offset arithmetic, explicit ownership, and comments
that explain invariants rather than restating code. Tests should cover arbitrary
stream boundaries and both malformed ZIP metadata and hostile HTTP behavior.

## Releases

Releases are built by GitHub Actions when a `vMAJOR.MINOR.PATCH` tag is pushed.
Update the single-line `VERSION` file to `MAJOR.MINOR.PATCH`. CMake generates
the public version header from it, and both `zget_version()` and the HTTP
user-agent use the generated value. The workflow rejects a tag that differs
from `VERSION` and rejects a built CLI that reports another version.

The release matrix covers Linux on x86_64 and ARM64, plus macOS on Apple
Silicon and Intel. Each platform build runs the full test suite before
packaging. The publish job then creates SHA-256 checksums and provenance
attestations and publishes a GitHub release with direct asset links and a
comparison to the previous stable release.
