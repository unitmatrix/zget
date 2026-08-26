# Roadmap

This roadmap describes the project's current direction, not release commitments.
Priorities may change as zget gains real-world usage and feedback.

zget has one primary purpose: fetch a requested member from a remote ZIP archive
without downloading the complete archive. Supplying `MEMBER` expresses that
selective-access contract; if the server cannot provide the required byte ranges,
the operation should fail rather than silently falling back to a full download.

## Recently completed

### Curl-compatible file output

Released in 0.5.0.

- Ordinary `-o FILE` streams directly to the requested path, truncating and
  overwriting existing regular files through normal file-open semantics.
- `-o -` selects standard output.
- Symlinks, FIFOs, devices, and other special paths follow normal platform open
  behavior instead of being rejected solely because the path exists.
- If extraction fails after writing begins, partial named output remains.
- README, `zget(1)`, and regression coverage describe and enforce the same
  contract.

### Graceful non-Range fallback

Released in 0.4.0, then removed in 0.6.0 to restore the strict selective-access
contract.

- 0.4.0 added a transparent complete-download fallback when a server ignored
  required Range requests.
- 0.6.0 removed that path so selective member retrieval never silently downloads
  the entire archive.

### Installation and distribution

- Added concise installation guidance for release binaries.
- Documented runtime dependencies and current Linux/macOS baselines.
- Added a concrete Ubuntu 24.04 x86_64 installation example.
- Keep distribution simple for now: use the existing versioned release archives
  rather than adding installer scripts, extra release assets, taps, or packages.

### Restore strict selective member retrieval

Released in 0.6.0.

- Removed the transparent complete-download and local-file fallback paths.
- Required valid HTTP Range responses for extraction and listing.
- Kept the CLI focused on exact member retrieval and explicit listing without
  adding whole-resource or range-policy modes.
- Updated implementation, tests, README, and `zget(1)` together.

### Minimize the public library API

Released in 0.6.0.

- Reduced the public API to one-shot streaming `zget_get()` and `zget_list()`
  operations plus stable error text and runtime version reporting.
- Internalized options, global initialization, and context lifecycle.
- Made listing names semantic UTF-8 through the ZIP UTF-8 flag, Info-ZIP
  Unicode Path, or CP437 fallback, with exact extraction against those names.
- Exposed compact Central Directory metadata with resolved sizes, CRC32,
  numeric compression method, and UTC Unix modification time.
- Updated the CLI, documentation, package consumers, and focused regression
  coverage for the intentional pre-1.0 API/ABI break.

### Canonical C usage example

Completed in PR #41 and released in 0.6.0.

- The CLI remains on the same public `zget_get()` / `zget_list()` API available
  to third-party callers; it has no privileged internal shortcut.
- README now quotes the real `cli/zget.c` extraction path as the canonical
  library example instead of maintaining a separate toy implementation.

## Next

### Real-world adoption and feedback

- Exercise zget against real large hosted ZIP workloads such as datasets,
  release/build artifacts, firmware archives, and object-storage/CDN content.
- Let real usage determine whether any additional query, filtering, progress, or
  format features are worth adding.
- Pay particular attention to interoperability with imperfect but common ZIP
  producers, HTTP behavior in the wild, diagnostics, and slow-peer/timeout
  behavior before adding new architecture.
- Do not assign the next minor version or plan another public API expansion until
  real usage gives a concrete reason.

## Deferred

### Exact member metadata lookup

- Consider an optional exact-member metadata convenience only if a real need
  appears to inspect one member without downloading its payload.
- Prefer reusing the existing streaming Central Directory parser and early-stop
  behavior over introducing speculative `stat`/`exists` APIs.
- Do not add it solely for API completeness.

### Benchmark and prove the value proposition

- Keep benchmark execution deferred until it is explicitly resumed; the agreed
  downloader-oriented benchmark design remains applicable.
- Add reproducible benchmarks for representative large remote ZIP workloads.
- Measure transferred bytes, memory use, time to first result, and total time for
  targets at different positions in the Central Directory.
- Compare with conventional full-download workflows and similar remote-ZIP tools
  where workloads are equivalent and the comparison is fair.
- Use the results to make the README's opening value proposition concrete and
  measurable.

## Planned

### Robustness and performance

- Expand malformed ZIP/ZIP64 and strict Range-path coverage as real failures are
  found.
- Fuzz new parsing paths as they are introduced.
- Benchmark archives with hundreds of thousands, millions, and larger entry
  counts as implementation changes warrant it.

### Distribution maturity

- Keep upstream builds, installs, metadata, and releases friendly to Linux
  distribution maintainers.
- Pursue broader distribution inclusion after further field testing.

## Future formats

Evaluate additional formats only when selective remote byte-range access provides
a demonstrated advantage. Do not generalize libzget into a format framework in
advance of a real second-format use case.

Not currently planned:

- public transport-neutral range-engine/state-machine API
- generic range-driven file-format framework
- whole-resource download mode
- speculative `--stat`, `--exists`, globbing, or machine-readable query modes
- interactive progress features without demonstrated demand
- generic archive-manager functionality
- FUSE/mount support
- plugin frameworks or speculative component APIs
