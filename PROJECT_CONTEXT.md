# Project working context

This file is a compact continuity note for future work on zget. It is not
product documentation and is intentionally kept only on the `assistant/context`
branch.

Keep here only decisions that would be costly to reconstruct from chat history:
architecture choices, explicitly rejected approaches, release state, and working
rules. Do not duplicate roadmap items or user-facing documentation.

## Current state

- Latest release: `0.6.0`.
- Release `0.6.0` contains the restored strict selective-access contract, the
  minimal one-shot public library API, semantic UTF-8 ZIP member names and
  metadata, and whole-archive `zget_list()` / CLI `-l` / `-1` listing behavior.
- `VERSION` is the single source of truth for the project version. Do not add
  manually synchronized version literals to tests or build plumbing.
- The canonical C extraction example in README now quotes the same public
  `zget_get()` path used by `cli/zget.c` (PR #41).

## Product direction

- zget has one primary job: fetch a requested member from a remote ZIP archive
  without downloading the complete archive.
- `libzget` is the small C library that solves that complete job for callers:
  HTTP Range access, ZIP/ZIP64 navigation, selective payload retrieval,
  STORE/DEFLATE handling, size/CRC validation, and streaming uncompressed output.
- Keep the simplest public operation as the center of the library:

      zget_get(url, member, write_cb, userdata)

  A caller that only wants one remote member should not need to understand ZIP
  ranges, local headers, decompression, or a parser state machine.
- `zget_list()` remains the small companion navigation operation for streaming a
  complete Central Directory listing.
- Supplying `MEMBER` expresses a selective operation. If the server cannot
  provide the required ranges, fail instead of silently downloading the complete
  archive.
- Do not add whole-resource retrieval (`zget URL`); curl already serves that
  purpose.
- Prefer real user demand over speculative public abstractions or archive-manager
  features.

## Public API decisions

- Keep the public library surface centered on two one-shot streaming operations:
  `zget_get(url, member, write_cb, userdata)` for extraction and
  `zget_list(url, list_cb, userdata)` for listing.
- Keep `zget_write_cb` with opaque `userdata`; extraction remains push-based and
  streaming so callers can write to files, memory, sockets, hashes, parsers, or
  other sinks without buffering the complete member. Callback return zero means
  continue; nonzero aborts with `ZGET_ECALLBACK`. The listing callback follows
  the same rule.
- Keep options, process-wide initialization, source objects, format objects, and
  parser lifecycle internal unless real usage demonstrates a public need.
- Do not expose a public transport-neutral `process -> needed range -> provide`
  state machine merely because the internal ZIP parser can be expressed that
  way. That design was explored and explicitly rejected as the primary API
  because it pushes HTTP/range/decompression complexity onto ordinary C callers.
- Do not generalize libzget into a generic range-driven file-format framework on
  speculation. ZIP is the problem being solved.
- Keep `zget_error_string()` for human-readable result text and `zget_version()`
  for runtime version reporting.
- Public operations return `int`; `zget_error` remains the named constants. The
  target public error set is `ZGET_OK`, `ZGET_EINVAL`, `ZGET_EHTTP`,
  `ZGET_ERANGE`, `ZGET_ECHANGED`, `ZGET_EZIP`, `ZGET_EUNSUPPORTED`,
  `ZGET_ENOTFOUND`, `ZGET_ECOMPRESSION`, `ZGET_ECRC`, `ZGET_ECALLBACK`, and
  `ZGET_ENOMEM`.
- `zget_list()` lists the whole archive only. Do not add an optional member
  selector solely for completeness; revisit exact metadata lookup only when a
  concrete use case appears.
- Listing metadata should expose useful ZIP Central Directory data at its known
  semantic type, without requiring callers to understand incidental ZIP
  machinery.
- The public API shape stays unchanged during the minizip-ng migration. ZIP name,
  timestamp, and extra-field interpretation should follow minizip-ng semantics as
  provided by the low-level `mz_zip` API. Do not retain zget-specific semantic
  overrides for Info-ZIP Unicode Path `0x7075`, Extended Timestamp `0x5455`, or
  similar ZIP details merely to preserve the old in-project parser behavior.
- `zget_get()` continues to perform exact member selection using the member name
  exposed through the chosen ZIP semantics. Do not add path normalization,
  Unicode normalization, slash translation, or locale-dependent matching.
- If multiple Central Directory entries resolve to the same member name,
  `zget_get()` selects the first matching entry in Central Directory order.
  `zget_list()` still emits every entry. This preserves streaming early-stop
  lookup.
- Do not expose API-versioning machinery such as `struct_size` in
  `zget_member_info` without a demonstrated need in this pre-1.0 API.
- Do not expose raw general-purpose ZIP flags or library-invented derived member
  flags. Keep compression method as numeric `uint16_t`, CRC32 as `uint32_t`, and
  resolved compressed and uncompressed byte sizes as `uint64_t`.
- Keep the public `int64_t mtime` representation as Unix timestamp seconds since
  1970-01-01 UTC; derive it through minizip-ng rather than maintaining a separate
  zget-specific `0x5455`/NTFS/DOS timestamp precedence implementation.
- Do not expose `external_attributes` without a demonstrated use case.
- Keep `ZGET_API` visibility plumbing and generated version macros; they are
  normal build/ABI infrastructure, not functional API bloat.

## Internal architecture

- Keep transport independence as an internal implementation boundary, not a
  public product contract.
- HTTP Range remains zget's responsibility. minizip-ng must not own HTTP access or
  change the public transport contract.
- Use minizip-ng as the selected ZIP library, specifically the low-level `mz_zip`
  API, for ZIP/ZIP64, Central Directory, and local-header parsing.
- The ZIP layer should consume zget's private source/stream adapter rather than
  call libcurl directly.
- Preserve selective-access efficiency as an architectural invariant: fetching
  one member payload must require exactly one HTTP Range request for that payload.
- Read the Central Directory through one forward HTTP Range request and retain
  early-stop lookup. Memory used while scanning the Central Directory must remain
  O(1) with respect to Central Directory size and entry count.
- Do not introduce a replay buffer that grows with the Central Directory. A small
  fixed replay window is allowed only to satisfy minizip-ng's access pattern.
  Confirmed behavior to support: `mz_zip_open()` reads the four-byte Central
  Directory signature, then `goto_first()` seeks back to the beginning of the
  Central Directory.
- Serve EOCD/tail access from a fixed-size tail buffer.
- In the first migration stage, decompression and CRC streaming may remain on the
  existing zlib path while minizip-ng owns ZIP/ZIP64/CD/local-header parsing.
- Migration acceptance criterion: HTTP request count and memory behavior must be
  no worse than current zget.
- The earlier decision to keep the in-project ZIP/Central Directory parser and to
  treat the minizip-ng investigation as closed is superseded by this decision.

## HTTP Range decisions

- Do not require or trust `Accept-Ranges` as a prerequisite. Issue the actual
  Range request and validate the response that the server returns.
- A successful archive-data Range response must be `206 Partial Content` with a
  valid `Content-Range` matching the requested interval and a usable total object
  size.
- `Content-Length` is optional for Range responses. Chunked or otherwise
  length-unframed HTTP bodies are acceptable when the Range response itself is
  valid.
- The actual number of received body bytes must exactly match the requested
  interval length. If `Content-Length` is present, it is an additional
  consistency check and must agree with that expected body length.
- A `200` response to a required Range request, malformed or inconsistent
  `Content-Range`, contradictory `Content-Length`, or wrong actual body length is
  an error. Do not turn any of these cases into a complete-download fallback.
- Preserve object consistency across requests using stable total size and a
  strong ETag/`If-Match` when available.
- Keep the suffix-range syntax compatibility fallback: when `bytes=-N` is
  rejected or ignored with the selected compatibility statuses, use a one-byte
  `bytes=0-0` probe to learn total size and retry the equivalent explicit tail
  interval. This remains selective access and is distinct from a full-download
  fallback.

## CLI role

- The `zget` CLI is the canonical reference consumer of public `libzget`.
- Keep CLI extraction and listing on the same public `zget_get()` / `zget_list()`
  path available to third-party C callers; do not give the CLI privileged
  internal shortcuts.
- Keep the relevant CLI integration deliberately simple and idiomatic so actual
  snippets from `cli/zget.c` can be quoted in the README as examples of correct
  library usage.
- CLI-specific concerns such as argument parsing, output-file handling, terminal
  safety, formatting, and SIGPIPE behavior stay outside libzget.

## CLI output behavior

- Since 0.5.0, ordinary `-o FILE` follows curl-style output semantics.
- Extracted bytes stream directly to the requested path through normal file-open
  behavior. Existing regular files are truncated/overwritten, and partial output
  remains if extraction fails after writing has started.
- `-o -` selects standard output.
- Existing symlinks, devices, and FIFOs are not rejected merely because the path
  exists; they follow normal platform open behavior.
- The earlier atomic no-clobber publication path was deliberately removed. Do not
  restore it as the ordinary `-o` contract without a new decision.

## Benchmark decisions

- Keep benchmark execution deferred until explicitly resumed.
- The agreed benchmark design remains the downloader-oriented comparison because
  that is again the product being built: released zget versus `remotezip`,
  `unzip-http`, and a conventional full-download `curl` + `unzip` baseline.
- Give every tool the same generated ZIP, exact member, and local Range-capable
  HTTP server so the workload is equivalent.
- Record transferred bytes, HTTP request count, time to first output byte, total
  wall time, CPU time, and peak resident memory.
- Cover targets near the start, middle, and end of the Central Directory, plus a
  missing member. Start with 100k entries and include the 1m-entry scale case.
- Run via a manually triggered GitHub Actions workflow; keep generated benchmark
  archives out of the repository and publish a permanent result only through a
  deliberate reviewable change.

## Earlier approaches explicitly dropped

- Transparent complete-download fallback.
- Sequential non-Range archive parsing as a fallback path.
- `--range-only`, `--no-fallback`, or configurable full-download limits.
- Whole-resource `zget URL` mode.
- Public transport-neutral range-engine/state-machine API as the primary libzget
  interface.
- Generic range-driven file-format framework.
- Generic archive-manager scope, FUSE/mount support, and speculative plugin
  frameworks.
- Speculative `--stat`, `--exists`, globbing, machine-readable query modes, or
  interactive progress without demonstrated demand.

## Release/API compatibility

- libzget remains pre-1.0: minor releases may intentionally revise API/ABI; patch
  releases preserve the ABI of their minor line.
- Release `0.6.0` is the first released line containing the minimal one-shot API
  adopted in PR #37.
- Do not assign the next minor version or plan another API break without a
  concrete reason from real usage.

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
