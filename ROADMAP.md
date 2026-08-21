# Roadmap

This roadmap describes the project's current direction, not release commitments.
Priorities may change as zget gains real-world usage and feedback.

zget prioritizes completing the requested operation. Byte-range access is the
preferred optimization, not a requirement: when a server cannot provide usable
ranges, zget should fall back to the least expensive safe retrieval strategy and
only download the complete archive when necessary.

## Next

### Graceful non-Range fallback

- Prefer HTTP Range when available.
- Fall back to sequential retrieval when Range is unavailable.
- Stop early when a requested member can be located and extracted safely.
- Download and spool the complete archive only when authoritative ZIP metadata
  is required to complete extraction.
- `-l` and `-1` remain Central Directory based; without Range they may require
  downloading the complete archive before producing a listing.
- Add `--range-only` for callers that require Range access and do not permit a
  sequential or full-download fallback.
- Add a configurable download limit so embedding applications and CLI users can
  bound fallback network cost when desired. The normal CLI remains focused on
  completing the requested operation rather than imposing a small default cap.

### Transfer visibility

- Add an interactive progress meter for long downloads and extractions.
- Keep stdout reserved for requested data; progress belongs on stderr.
- Avoid interactive progress output when stderr is not a terminal.

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
- Fuzz new sequential parsing paths as they are introduced.
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
