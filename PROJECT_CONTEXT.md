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
  other sinks without buffering the complete member.
- Remove public `zget_options`, `zget_options_init()`, process-wide
  `zget_global_init()` / `zget_global_cleanup()`, `zget_ctx`, and the public
  open/extract/close context API. Their implementation concerns should stay
  internal unless real usage later demonstrates a need for public control.
- Remove `zget_last_error_message()` with the context API. Keep
  `zget_error_string()` for human-readable result text and `zget_version()` for
  runtime version reporting.
- Keep only public error codes that can actually be returned by `zget_get()` or
  `zget_list()` after the API simplification; remove unreachable/internal-only
  public result codes such as `ZGET_ENOTINITIALIZED` once global init is
  internalized.
- `zget_list()` lists the whole archive only. Do not give it an optional member
  selector; exact member extraction belongs to `zget_get()`.
- Listing metadata should expose useful ZIP Central Directory fields without
  inventing higher-level interpretations: raw name bytes plus `name_length`, raw
  general-purpose `flags`, raw DOS `modified_time` and `modified_date`, resolved
  `compressed_size` and `uncompressed_size`, `crc32`, and `compression_method`.
- Do not expose derived `ZGET_MEMBER_NAME_UTF8` /
  `ZGET_MEMBER_HAS_MODIFIED_TIME` flags or decomposed calendar fields in the
  public metadata structure. The CLI may interpret raw metadata for display.
- Do not add `external_attributes` to the public API without a demonstrated use
  case. If needed later, prefer exposing the raw ZIP field before adding
  platform-specific interpretation.

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
