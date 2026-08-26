# Roadmap

This roadmap describes the project's current direction, not release commitments.
Priorities may change as zget gains real-world usage and feedback.

zget is moving toward a transport-neutral ZIP range engine. The library should
understand ZIP/ZIP64 structure and determine which byte ranges a caller needs,
while the caller owns transport, storage, retries, authentication, caching, and
payload handling.

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

Released in 0.4.0, but no longer aligned with the current product direction.

- 0.4.0 added a transparent complete-download fallback when a server ignored
  required Range requests.
- Main later removed that fallback so selective access never silently downloads
  the complete archive.

### Restore strict selective member retrieval

Completed on main after 0.5.0.

- Removed transparent complete-download and local-file fallback paths from the
  downloader-oriented implementation.
- Required valid HTTP Range responses for extraction and listing.
- Kept the CLI focused on exact member retrieval and explicit listing.

### Minimize the public library API

Completed on main after 0.5.0, but now serves as the baseline for a further
pre-1.0 redesign.

- Reduced the public API to one-shot `zget_get()` and `zget_list()` operations.
- Internalized options, global initialization, and context lifecycle.
- Added semantic UTF-8 member names and compact Central Directory metadata.
- PR #39 made CLI listing whole-archive only.

## Next

### Transport-neutral public API

Redesign libzget around one incremental caller-driven state machine.

- Remove URL/HTTP/libcurl ownership from the public library contract.
- Let the engine report the logical byte range it needs next; the caller fetches
  or reads those bytes using its own transport/storage stack and provides them
  back to libzget.
- Support arbitrary transport chunking while preserving parser state across
  fragments.
- Exact lookup should stop Central Directory consumption as soon as the first
  semantic name match is found, then resolve the local header and return payload
  location/metadata.
- Listing should use the same range/provide mechanism and emit entries
  incrementally while walking the complete Central Directory.
- Prefer pull-style state inspection over synchronous transport callbacks so the
  same API fits both blocking clients such as libcurl and asynchronous hosts such
  as Chromium.
- Keep the central opaque state naming minimal (`zget`); finalize function names
  only after checking established C-library practice.

### Make the CLI the canonical integration example

- Refactor `zget` CLI to use only the public transport-neutral libzget path for
  ZIP navigation and range planning.
- Keep the integration intentionally small and idiomatic so snippets from
  `cli/zget.c` can be quoted directly in the README as the recommended usage
  example.
- Keep libcurl in the CLI/reference adapter for HTTP Range requests and zlib in
  the CLI/reference payload path for STORE/DEFLATE handling and CRC validation.
- Do not give the CLI privileged internal shortcuts unavailable to third-party
  consumers.

### Rework tests around the new boundary

- Add direct public-API tests for `process -> needed range -> provide -> process`.
- Exercise fragmented range delivery, early Central Directory stop, missing
  members, ZIP64, malformed metadata, and full-directory listing without any
  HTTP dependency.
- Preserve focused parser fuzzing.
- Keep HTTP correctness tests around the CLI/reference libcurl adapter rather
  than treating HTTP policy as a core libzget contract.

## After the redesign

### Real-world adoption and integration feedback

- Exercise the range engine with more than one caller model, starting with the
  reference libcurl CLI and at least one non-HTTP or asynchronous-style test
  harness.
- Test against real large hosted ZIP workloads such as datasets, build/release
  artifacts, firmware archives, and object-storage/CDN content.
- Let real integrations determine whether further API surface is justified.

## Deferred

### Exact member metadata-oriented convenience

- The transport-neutral exact lookup naturally returns payload location and
  member metadata without downloading the payload.
- Do not add separate `stat`/`exists` style APIs unless a concrete consumer needs
  additional convenience beyond that core lookup result.

### Small bounded semantic results

- Do not require streaming merely for consistency when a result is naturally
  small and bounded.
- No special public EOCD-return API is planned now. Revisit whole-value exposure
  for EOCD or other bounded structures only if a concrete integration benefits
  from it.

### Benchmark and prove the value proposition

- Keep benchmark execution deferred until the transport-neutral redesign lands.
- Revisit the old benchmark design before running it; it was built around the
  downloader-oriented API.
- Measure the new engine's useful properties: metadata bytes required before a
  member is located, early-stop savings by Central Directory position, memory,
  processing overhead, and integration cost.

## Planned

### Robustness and performance

- Expand malformed ZIP/ZIP64 coverage as real failures are found.
- Fuzz new parser/state-machine paths as they are introduced.
- Exercise archives with hundreds of thousands, millions, and larger entry
  counts when implementation changes warrant it.

### Distribution maturity

- Keep upstream builds, installs, metadata, and releases friendly to Linux
  distribution maintainers.
- Pursue broader distribution inclusion after the new API has field testing.

## Future formats

Stay ZIP-focused for now. Evaluate additional formats only when selective remote
byte-range access provides a demonstrated advantage and the core abstraction has
proved reusable in practice.

Not currently planned:

- whole-resource download mode
- speculative `--stat`, `--exists`, globbing, or machine-readable query modes
- interactive progress features without demonstrated demand
- generic archive-manager functionality
- FUSE/mount support
- plugin frameworks or speculative component APIs
