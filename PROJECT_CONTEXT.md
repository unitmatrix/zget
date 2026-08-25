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
  in 0.4.0 when a server ignores required Range requests. The current product
  direction is to remove that fallback and return to a strict selective-access
  contract.
- The earlier ideas for a sequential non-Range parser, `--range-only`, and a
  configurable fallback download limit were explicitly dropped. Do not
  resurrect them without a new decision.

## Product direction

- zget has one primary job: fetch only the requested member from a remote
  archive. Keep the product and implementation as small as practical around
  that goal.
- `libzget` is a standalone public library, not merely an implementation detail
  of the CLI. Keep its public functionality minimal around selective remote ZIP
  member lookup and extraction rather than expanding it into a general HTTP or
  archive API.
- The CLI remains a thin frontend over that library. Existing `-l` / `-1`
  listing support remains as small navigation functionality, but should not
  drive speculative query APIs or broader archive-manager scope.
- `zget URL MEMBER` is a selective operation. It must use remote byte-range
  access; if the server cannot provide the required ranges, fail instead of
  silently downloading the complete archive.
- Do not add whole-resource retrieval (`zget URL`). curl already serves that
  purpose; zget should remain differentiated by archive-member selection.
- Do not add flags merely to recover the selective contract, such as
  `--range-only` or `--no-fallback`. Supplying `MEMBER` already expresses it.
- Prefer real user demand over speculative CLI features or abstractions.
- Optimize discover-to-useful-result time to roughly 30 seconds and back the
  value proposition with reproducible measurements.

## Public API decisions

- Minimize the public library surface around two one-shot streaming operations:
  `zget_get(url, member, write_cb, userdata)` for extraction and
  `zget_list(url, list_cb, userdata)` for listing.
- Keep `zget_write_cb` with opaque `userdata`; extraction remains push-based and
  streaming so callers can write to files, memory, sockets, hashes, parsers, or
  other sinks without buffering the complete member. Callback return zero means
  continue; nonzero aborts with `ZGET_ECALLBACK`. The listing callback follows
  the same rule.
- Remove public `zget_options`, `zget_options_init()`, process-wide
  `zget_global_init()` / `zget_global_cleanup()`, `zget_ctx`, and the public
  open/extract/close context API. Their implementation concerns should stay
  internal unless real usage later demonstrates a need for public control.
- Remove `zget_last_error_message()` and `zget_last_error()` with the context
  API. Keep `zget_error_string()` for human-readable result text and
  `zget_version()` for runtime version reporting.
- Public operations continue to return `int`; `zget_error` remains the named
  constants. The target public error set is `ZGET_OK`, `ZGET_EINVAL`,
  `ZGET_EHTTP`, `ZGET_ERANGE`, `ZGET_ECHANGED`, `ZGET_EZIP`,
  `ZGET_EUNSUPPORTED`, `ZGET_ENOTFOUND`, `ZGET_ECOMPRESSION`, `ZGET_ECRC`,
  `ZGET_ECALLBACK`, and `ZGET_ENOMEM`. Remove backend/internal-only codes such
  as `ZGET_EDEFLATE`, `ZGET_EIO`, `ZGET_ELIMIT`, and `ZGET_ENOTINITIALIZED`.
- `zget_list()` lists the whole archive only. Do not give the library API an
  optional member selector; exact member extraction belongs to `zget_get()`.
- Listing metadata should expose useful ZIP Central Directory data at its known
  semantic type, without requiring callers to understand incidental ZIP
  machinery.
- Public member names are valid UTF-8, NUL-terminated strings represented as
  `const char *name` plus `size_t name_length`; `name_length` is the byte length
  of the exposed UTF-8 string and excludes the terminator. Resolve names in this
  order: GP bit 11 UTF-8 first; otherwise a usable Info-ZIP Unicode Path extra
  field `0x7075` (version 1, matching CRC32, valid UTF-8); otherwise CP437 to
  UTF-8. Embedded NUL in the selected name is malformed ZIP. An unusable
  `0x7075` is ignored and falls back to the standard-name path; a structurally
  malformed overall extra-field block is `ZGET_EZIP`.
- `zget_get()` accepts a NUL-terminated valid UTF-8 member name and performs an
  exact case-sensitive comparison against the same resolved semantic UTF-8 name
  exposed by `zget_list()`. Do not normalize paths or Unicode, translate slash
  styles, apply locale behavior, or impose the raw ZIP 65535-byte filename limit
  on the resolved UTF-8 input. A name returned by listing should be directly
  usable with `zget_get()`.
- If multiple Central Directory entries resolve to the same member name,
  `zget_get()` selects the first matching entry in Central Directory order.
  `zget_list()` still emits every entry. This matches libzip lookup behavior and
  preserves streaming early-stop lookup.
- Remove `struct_size` and `ZGET_MEMBER_INFO_V1_SIZE` from `zget_member_info`;
  they are API-versioning machinery rather than archive metadata and are not
  justified for this pre-1.0 minimal surface.
- Do not expose raw general-purpose ZIP flags or library-invented derived member
  flags. Keep compression method as its numeric `uint16_t` method ID, CRC32 as
  `uint32_t`, and resolved compressed and uncompressed byte sizes as `uint64_t`.
  Do not replace method IDs with a closed enum that would prevent listing
  unknown methods.
- Represent modification time as one public `int64_t mtime`: Unix timestamp
  seconds since 1970-01-01 UTC. Resolve it in priority order: NTFS extra field
  `0x000a`, then Extended Timestamp `0x5455`, then legacy DOS date/time
  interpreted as UTC. NTFS subsecond precision is intentionally discarded.
  The DOS-as-UTC rule is a deterministic fallback for a timezone-less legacy
  representation; do not depend on the process timezone, locale, or DST.
- Do not add `external_attributes` to the public API without a demonstrated use
  case. If needed later, expose the meaningful field with as little
  platform-specific interpretation as possible.
- Keep `ZGET_API` visibility plumbing and the generated `zget_version.h` version
  macros; they are normal build/ABI infrastructure, not functional API bloat.

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
- Keep the suffix-range syntax compatibility fallback introduced in PR #23:
  when `bytes=-N` is rejected or ignored with the selected compatibility
  statuses, use a one-byte `bytes=0-0` probe to learn the total size and retry
  the equivalent explicit tail interval. This remains selective access and is
  distinct from the complete-archive fallback that is to be removed.

## ZIP implementation boundaries

- Keep the current architecture: HTTP Range through zget/libcurl, a small zget
  streaming ZIP/Central Directory parser, and zlib for DEFLATE + CRC.
- The investigation of libzip, classic MiniZip, minizip-ng, miniz, libarchive,
  and zziplib as replacement ZIP dependencies is closed. Do not re-propose a
  third-party archive library unless the direction is explicitly reopened.

## Benchmark decisions

- Run the benchmark in GitHub Actions via a manually triggered workflow so no
  local benchmark dependencies need to be installed.
- Benchmark the current released zget binary downloaded from GitHub Releases; do
  not build zget from source in the benchmark workflow.
- Compare zget with `remotezip`, `unzip-http`, and a conventional full-download
  `curl` + `unzip` baseline.
- Give every tool the same generated ZIP, exact member, and local Range-capable
  HTTP server so the workload is equivalent.
- Record transferred bytes, HTTP request count, time to first output byte, total
  wall time, CPU time, and peak resident memory.
- Cover targets near the start, middle, and end of the Central Directory, plus a
  missing member. Start with 100k entries and include the 1m-entry scale case.
- Keep generated benchmark archives out of the repository.
- Keep result handling simple: the workflow produces one human-readable
  `results.md` artifact. Do not add a JSON/CSV/render pipeline unless there is a
  demonstrated need for it.
- The benchmark workflow must not modify the repository or automatically open a
  PR. A permanent `benchmarks/results.md` snapshot is published only by
  deliberately selecting a benchmark run and committing that result through a
  normal reviewable change.

## CLI output behavior

- Since 0.5.0, ordinary `-o FILE` follows curl-style output semantics.
- Extracted bytes stream directly to the requested path through normal file-open
  behavior. Existing regular files are truncated/overwritten, and partial output
  remains if extraction fails after writing has started.
- `-o -` selects standard output.
- Existing symlinks, devices, and FIFOs are not rejected merely because the path
  exists; they follow normal platform open behavior.
- The earlier `lstat`/same-directory `mkstemp`/`fsync`/`link` atomic no-clobber
  publication path was deliberately removed. Do not restore atomic validated
  publication as the ordinary `-o` contract without a new decision.

## Authoritative project sources

- Roadmap and product priorities: `ROADMAP.md` on `docs/roadmap`.
- Release version: `VERSION` on `main` / the release branch.
- User-facing behavior and installation guidance: normal project documentation
  on `main`.

## Working conventions

- Before starting zget work, read this file and the current
  `docs/roadmap:ROADMAP.md`.
- Prefer small, focused PRs with one coherent purpose.
- Diagnose CI failures from the failing job/test/log before changing code.
- Add focused regression coverage with behavior changes and keep documentation
  synchronized with behavior.
- Avoid speculative refactors and duplicated sources of truth.
- Before recommending an API shape, data representation, or format-facing
  behavior, first check the relevant standard and established industry/library
  practice. Then recommend the smallest design that fits zget; do not invent a
  custom convention when a well-established C/library convention already fits.
- When a roadmap item is completed, reprioritized, added, dropped, or otherwise
  changes status, update `docs/roadmap:ROADMAP.md` in the same work session. Do
  not record a roadmap status change only in this context file.
- `фиксируй`, `зафиксируй`, and `lock this in` mean the same thing. Before
  persisting anything, first show a short list of the proposed durable decisions
  and wait for confirmation or correction. After confirmation, persist them
  here; update the roadmap too whenever the decision changes roadmap status or
  priority.
- Before tagging a release: docs/readiness review, version bump through
  `VERSION`, green CI, then tag the exact green release commit.

## Continuity rule

Use this file for durable reasoning and working rules; use the roadmap for what
is next, planned, completed, or reprioritized. Keep both synchronized without
copying the roadmap into this file.
