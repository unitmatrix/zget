# Roadmap

This roadmap describes the project's current direction, not release commitments.
Priorities may change as zget gains real-world usage and feedback.

zget prioritizes completing the requested operation. Byte-range access is the
preferred optimization, not a requirement: when a server cannot provide usable
ranges, zget falls back to a safe complete download and continues through the
same local source and archive implementation.

## Recently completed

### Graceful non-Range fallback

Released in 0.4.0.

- Prefer HTTP Range when available.
- Transparently fall back to a complete temporary download when a server ignores
  required Range requests.
- Keep the fallback internal by using anonymous temporary storage and the normal
  local-file source path.
- Preserve normal extraction and listing behavior while documenting that the
  fallback necessarily transfers the complete archive.

### Installation and distribution

- Added concise installation guidance for release binaries.
- Documented runtime dependencies and current Linux/macOS baselines.
- Added a concrete Ubuntu 24.04 x86_64 installation example.
- Keep distribution simple for now: use the existing versioned release archives
  rather than adding installer scripts, extra release assets, taps, or packages.

## Next

### Benchmark and prove the value proposition

- Add reproducible benchmarks for representative large remote ZIP workloads.
- Measure transferred bytes, memory use, time to first result, and total time for
  targets at different positions in the Central Directory.
- Compare with conventional full-download workflows and similar remote-ZIP tools
  where workloads are equivalent and the comparison is fair.
- Use the results to make the README's opening value proposition concrete and
  measurable: zget should quickly demonstrate why fetching one member from a
  large remote archive is useful.

### Curl-compatible file output

- Make ordinary `-o FILE` follow curl-style output semantics: stream directly to
  the requested path, truncate/overwrite an existing file, and leave partial
  output when extraction fails after writing has started.
- Treat `-o -` as standard output.
- Remove the current atomic no-clobber temporary-file publication path for
  ordinary `-o` output.
- Update README, `zget(1)`, and regression coverage in the same functional PR.

### Remote archive queries

- Add an exact-member `--stat` operation.
- Add a script-friendly `--exists` operation with meaningful exit status.
- Add machine-readable listing and stat output, with a streaming-friendly format
  for listings.
- Add glob filtering for `-l` and `-1` while preserving existing exact-match
  semantics where they are already part of the public API.

### Transfer visibility

- Add an interactive progress meter for long downloads and extractions.
- Keep stdout reserved for requested data; progress belongs on stderr.
- Avoid interactive progress output when stderr is not a terminal.

## Planned

### Real-world adoption and feedback

- Exercise zget against real large hosted ZIP workloads such as datasets,
  release/build artifacts, firmware archives, and object-storage/CDN content.
- Use field feedback to prioritize compatibility and hardening work instead of
  speculative features.

### Robustness and performance

- Expand malformed ZIP/ZIP64 and non-Range fallback coverage.
- Fuzz new parsing paths as they are introduced.
- Benchmark archives with hundreds of thousands, millions, and larger entry
  counts as implementation changes warrant it.

### Distribution maturity

- Keep upstream builds, installs, metadata, and releases friendly to Linux
  distribution maintainers.
- Pursue broader distribution inclusion after further field testing.

## Future formats

Evaluate additional formats only when remote byte-range access provides a
meaningful advantage. Likely candidates include:

- ISO 9660
- SquashFS

Not currently planned:

- generic archive-manager functionality
- FUSE/mount support
- plugin frameworks or speculative component APIs
