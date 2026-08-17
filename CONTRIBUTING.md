# Contributing

Keep changes focused on fetching one exact ZIP member. Core ZIP parsing must
remain independent of HTTP, streaming, hostile-input safe, and O(1) in entry
count. New codecs or archive formats require an explicit product decision.

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
Before tagging, update the version in `CMakeLists.txt`, `include/zget.h`, and
`zget_version()` together. The workflow rejects a tag that differs from the
CMake project version and rejects a built CLI that reports another version.

The release matrix covers Linux on x86_64 and ARM64, plus macOS on Apple
Silicon and Intel. Each platform build runs the full test suite before
packaging. The publish job then creates SHA-256 checksums and provenance
attestations and publishes a GitHub release with direct asset links and a
comparison to the previous stable release.
