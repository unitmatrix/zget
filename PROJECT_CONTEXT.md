# Project working context

This file is a compact continuity note for future work on zget. It is not
product documentation and is intentionally kept only on the `assistant/context`
branch.

Keep here only decisions that would be costly to reconstruct from chat history:
architecture choices, explicitly rejected approaches, release state, and working
rules. Do not duplicate roadmap items or user-facing documentation.

## Current state

- Latest release: `0.5.0`.
- `VERSION` is the single source of truth for the project version. Do not add
  manually synchronized version literals to tests or build plumbing.
- Release 0.5.0 still contains the transparent complete-download fallback added
  in 0.4.0. Main removed that fallback in PR #36 and now requires selective
  byte-range access for extraction and listing.
- Main adopted the minimal one-shot public library API and semantic ZIP member
  names and metadata in PR #37. PR #39 then made CLI listing whole-archive only.
- Those post-0.5.0 API decisions are now an implementation baseline to migrate
  from: the product philosophy changed again before the next release.

## Product direction

- `libzget` is a **transport-neutral ZIP range engine**.
- Its job is to understand ZIP/ZIP64 structure and determine which byte ranges
  are needed to locate or enumerate members. It does not own URLs, HTTP, network
  requests, authentication, redirects, retries, proxies, caching, connection
  management, or file I/O.
- The caller owns the byte source and transport. Suitable hosts include libcurl,
  Chromium/network-service style asynchronous clients, download managers, object
  storage APIs, local sparse/random-access files, caches, and other range-capable
  sources.
- Exact member lookup should consume only as much Central Directory data as is
  needed to reach the first matching entry, then request the local header and
  return the member payload location and semantic metadata. A missing or late
  member may require the complete Central Directory.
- Listing uses the same range engine but walks the complete Central Directory and
  exposes entries incrementally.
- The final member payload is not libzget's transport responsibility. The caller
  fetches the returned payload range and may decode/verify it as appropriate.
- Keep the project narrowly ZIP-focused. Do not generalize into a generic archive
  framework or plugin architecture without demonstrated demand.

## Integration model

- Expose one integration model: an incremental, caller-driven state machine.
- The basic interaction is conceptually:

      process -> need range -> caller fetches/provides bytes -> process

  repeated until an entry/result is available or the operation is complete.
- Do not expose a synchronous `read_range()` callback as the fundamental public
  integration contract. That would force blocking I/O semantics onto async hosts
  such as Chromium.
- Prefer pull-style state inspection over transport callbacks: the caller asks
  what the engine needs next, performs I/O in its own architecture, supplies the
  bytes, then re-enters the engine.
- Public range requests should describe logical byte intervals. Transport chunks
  may split those intervals arbitrarily; libzget must preserve parser state across
  supplied fragments.
- The Central Directory parser remains genuinely incremental and supports early
  completion for exact lookup; it must not require buffering the whole directory.
- Do not make "everything streams" a dogma. Small, naturally bounded semantic
  values may be returned whole when that clearly simplifies the API, while large
  or potentially large data stays incremental. No special public EOCD-return API
  is currently planned; revisit only if a concrete use case appears.

## Public naming direction

- Avoid names such as `archive`, `resolver`, or redundant `zget_zip` for the
  central opaque state object.
- Prefer the minimal opaque type:

      typedef struct zget zget;

- Keep public terminology conventional and boring: `range` for a requested byte
  interval, and `provide` for supplying bytes at an offset. Avoid using `chunk`
  for logical ranges; a chunk is merely an arbitrary transport fragment.
- Exact final function names are not yet frozen. Before finalizing the header,
  check established C-library practice and keep the surface as small as possible.

## CLI role

- The `zget` CLI is the canonical reference consumer of `libzget`, not a privileged
  implementation using internal shortcuts.
- `cli/zget.c` should use only the same public libzget integration path available
  to third-party consumers for ZIP navigation/range planning.
- Keep that integration deliberately simple and idiomatic so real snippets from
  `cli/zget.c` can be quoted directly in the README as the recommended example.
- The CLI may use libcurl as its HTTP Range transport and zlib for payload decode
  and CRC verification, but those are CLI/integration responsibilities rather
  than public libzget responsibilities.
- Existing user-facing extraction and listing behavior can remain useful as the
  reference application even though the library underneath becomes transport
  neutral.

## ZIP semantic decisions to retain

- Public member names are semantic valid UTF-8. Resolve names in this order:
  GP bit 11 UTF-8, otherwise usable Info-ZIP Unicode Path `0x7075` version 1 with
  matching raw-name CRC32 and valid UTF-8, otherwise CP437 converted to UTF-8.
- Embedded NUL in the selected semantic name is malformed ZIP. Structurally
  malformed extra-field blocks are malformed ZIP; unusable optional `0x7075`
  falls back to the standard-name path.
- Exact member matching is case-sensitive against the same semantic UTF-8 name
  used by listing, with no path, Unicode, slash, or locale normalization.
- If multiple Central Directory entries resolve to the same name, the first
  Central Directory match wins for exact lookup; listing emits all entries.
- Useful member metadata remains semantic: compressed/uncompressed sizes as
  `uint64_t`, CRC32 as `uint32_t`, compression method as numeric `uint16_t`, and
  modification time as Unix UTC seconds in `int64_t` when exposed.
- Modification time resolution priority remains NTFS `0x000a`, Extended Timestamp
  `0x5455`, then DOS date/time interpreted deterministically as UTC. NTFS
  subseconds are intentionally discarded.
- Do not expose raw ZIP flags or external attributes without a demonstrated use
  case.
- Single-volume ZIP/ZIP64 remains the supported archive model. STORE and DEFLATE
  remain the payload methods understood by the reference CLI/extraction path.

## Existing implementation boundaries

- The current internal source/format split already points toward the new public
  model: ZIP asks an abstract source for ranges and is independent of libcurl.
- Refactoring should invert that dependency at the public boundary: instead of
  ZIP calling `source.read_range()`, the engine reports the next range and the
  host provides bytes.
- Keep the small in-project ZIP/Central Directory parser. The investigation of
  libzip, classic MiniZip, minizip-ng, miniz, libarchive, and zziplib as
  replacement ZIP dependencies is closed unless explicitly reopened.
- Preserve focused parser fuzzing and transport-independent format/source tests as
  the API is migrated.

## HTTP behavior after the pivot

- Strict HTTP validation remains valuable for the CLI/reference libcurl adapter,
  but it is no longer a libzget library contract.
- The reference HTTP consumer should continue to validate actual Range responses:
  `206`, matching `Content-Range`, identity representation, exact body length,
  stable object size, and strong ETag when available.
- A server ignoring a required Range must not silently turn the reference CLI
  into a complete-download path.
- The existing suffix-range compatibility fallback (suffix request rejected or
  ignored -> one-byte explicit probe -> explicit tail range) may remain in the
  CLI/reference adapter because it preserves selective access.

## Release/API compatibility

- libzget remains pre-1.0: minor releases may intentionally revise API/ABI; patch
  releases preserve the ABI of their minor line.
- The transport-neutral redesign is another intentional pre-1.0 API break and
  must not be shipped as a 0.5.x patch.
- `ZGET_API` visibility plumbing and generated version macros remain normal
  build/ABI infrastructure.

## Earlier approaches explicitly dropped

- Transparent complete-download fallback.
- Sequential non-Range archive parsing as a fallback path.
- `--range-only`, `--no-fallback`, or configurable full-download limits.
- Whole-resource `zget URL` mode.
- Generic archive-manager scope, FUSE/mount support, and speculative plugin
  frameworks.
- Speculative `--stat`, `--exists`, globbing, machine-readable query modes, or
  interactive progress without demonstrated demand.

## Benchmark note

- The previously designed benchmark targeted the downloader-oriented API and
  compared zget with remotezip, unzip-http, and curl+unzip.
- Keep benchmark execution deferred. Before resuming, revisit the benchmark so it
  measures the new transport-neutral range-engine value rather than blindly
  preserving assumptions from the old public API.

## Authoritative project sources

- Roadmap and product priorities: `ROADMAP.md` on `docs/roadmap`.
- Release version: `VERSION` on main / the release branch.
- User-facing behavior and installation guidance: normal project documentation
  on main.

## Working conventions

- Before starting substantive zget work, read this file and the current
  `docs/roadmap:ROADMAP.md`.
- Prefer small, focused PRs with one coherent purpose.
- Diagnose CI failures from the failing job/test/log before changing code.
- Add focused regression coverage with behavior changes and keep documentation
  synchronized with behavior.
- Avoid speculative refactors and duplicated sources of truth.
- Before recommending an API shape, data representation, or format-facing
  behavior, first check the relevant standard and established industry/library
  practice. Then recommend the smallest design that fits zget; do not invent a
  custom convention when established C/library practice fits.
- When a roadmap item is completed, reprioritized, added, dropped, or otherwise
  changes status, update `docs/roadmap:ROADMAP.md` in the same work session.
- `фиксируй`, `зафиксируй`, and `lock this in` mean the same thing. Before
  persisting anything, first show a short list of proposed durable decisions and
  wait for confirmation or correction. After confirmation, persist them here;
  update the roadmap too whenever the decision changes roadmap status or
  priority.
- Before tagging a release: docs/readiness review, version bump through `VERSION`,
  green CI, then tag the exact green release commit.

## Continuity rule

Use this file for durable reasoning and working rules; use the roadmap for what
is next, planned, completed, or reprioritized. Keep both synchronized without
copying the roadmap into this file.
