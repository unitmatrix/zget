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
- `zget URL MEMBER` is a selective operation. It must use remote byte-range
  access; if the server cannot provide the required ranges, fail instead of
  silently downloading the complete archive.
- Do not add whole-resource retrieval (`zget URL`). curl already serves that
  purpose; zget should remain differentiated by archive-member selection.
- Do not add flags merely to recover the selective contract, such as
  `--range-only` or `--no-fallback`. Supplying `MEMBER` already expresses it.
- Keep existing listing forms only as small supporting functionality; do not
  grow zget into a general archive manager.
- Prefer real user demand over speculative CLI features or abstractions.
- Optimize discover-to-useful-result time to roughly 30 seconds and back the
  value proposition with reproducible measurements.

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
- `фиксируй`, `зафиксируй`, and `lock this in` mean the same thing: persist
  durable decisions here; update the roadmap too whenever the decision changes
  roadmap status or priority.
- Before tagging a release: docs/readiness review, version bump through
  `VERSION`, green CI, then tag the exact green release commit.

## Continuity rule

Use this file for durable reasoning and working rules; use the roadmap for what
is next, planned, completed, or reprioritized. Keep both synchronized without
copying the roadmap into this file.
