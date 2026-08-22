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

## Next

### Transfer visibility

- Add an interactive progress meter for long downloads and extractions.
- Keep stdout reserved for requested data; progress belongs on stderr.
- Avoid interactive progress output when stderr is not a terminal.

### Installation documentation

- Add a short prerequisite section showing how to install the libraries needed
  to build and use zget on common Linux distributions and macOS.
- Keep the instructions concise and package-manager oriented, covering libcurl,
  zlib, and the required build tooling without duplicating full platform setup
  guides.

## Planned

### Remote archive queries

- Add glob filtering for `-l` and `-1` while preserving existing exact-match
  semantics where they are already part of the public API.
- Add an exact-member `--stat` operation.
- Add a script-friendly `--exists` operation with meaningful exit status.
- Add machine-readable listing and stat output, with a streaming-friendly format
  for listings.

### Robustness and performance

- Expand malformed ZIP/ZIP64 and non-Range fallback coverage.
- Fuzz new parsing paths as they are introduced.
- Benchmark archives with hundreds of thousands, millions, and larger entry
  counts.
- Measure transferred bytes, memory use, time to first result, and total time;
  compare with similar remote-ZIP tools where the workloads are equivalent.

### Distribution

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
