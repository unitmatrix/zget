# Roadmap

This roadmap describes the project's current direction, not release commitments.
Priorities may change as zget gains real-world usage and feedback.

zget has one primary purpose: fetch only a requested member from a remote archive
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

Released in 0.4.0, but no longer aligned with the current minimal product goal.

- Prefer HTTP Range when available.
- 0.4.0 added a transparent complete-download fallback when a server ignored
  required Range requests.
- Current direction: remove that fallback so selective member retrieval never
  silently downloads the entire archive.

### Installation and distribution

- Added concise installation guidance for release binaries.
- Documented runtime dependencies and current Linux/macOS baselines.
- Added a concrete Ubuntu 24.04 x86_64 installation example.
- Keep distribution simple for now: use the existing versioned release archives
  rather than adding installer scripts, extra release assets, taps, or packages.

## Next

### Restore strict selective member retrieval

- Keep extraction syntax focused on `zget [-o FILE] URL MEMBER`.
- Remove transparent complete-download fallback for member extraction.
- If required HTTP Range access is unavailable, fail clearly instead of
  downloading the full archive.
- Do not add whole-resource retrieval (`zget URL`); use curl for that job.
- Do not add `--range-only`, `--no-fallback`, or similar mode flags: providing
  `MEMBER` already defines the selective behavior.
- Update implementation, regression tests, README, and `zget(1)` together.

### Benchmark and prove the value proposition

- Keep benchmark execution deferred until it is explicitly resumed; the agreed
  benchmark design remains unchanged.
- Add reproducible benchmarks for representative large remote ZIP workloads.
- Measure transferred bytes, memory use, time to first result, and total time for
  targets at different positions in the Central Directory.
- Compare with conventional full-download workflows and similar remote-ZIP tools
  where workloads are equivalent and the comparison is fair.
- Use the results to make the README's opening value proposition concrete and
  measurable: zget should quickly demonstrate why fetching one member from a
  large remote archive is useful.

## Planned

### Real-world adoption and feedback

- Exercise zget against real large hosted ZIP workloads such as datasets,
  release/build artifacts, firmware archives, and object-storage/CDN content.
- Let real usage determine whether any additional query, filtering, progress, or
  format features are worth adding.

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
a demonstrated advantage. Likely candidates include:

- ISO 9660
- SquashFS

Not currently planned:

- whole-resource download mode
- speculative `--stat`, `--exists`, globbing, or machine-readable query modes
- interactive progress features without demonstrated demand
- generic archive-manager functionality
- FUSE/mount support
- plugin frameworks or speculative component APIs
