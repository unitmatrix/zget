# Project working context

This file is a compact continuity note for future work on zget. It is not
product documentation and is intentionally kept only on the `assistant/context`
branch.

Keep here only decisions that would be costly to reconstruct from chat history:
architecture choices, explicitly rejected approaches, release state, and working
rules. Do not duplicate roadmap items or user-facing documentation.

## Current state

- Latest release: `0.4.0`.
- `VERSION` is the single source of truth for the project version. Do not add
  manually synchronized version literals to tests or build plumbing.
- HTTP Range is the preferred efficient path, not a hard requirement.
- Since 0.4.0, if a server ignores a required Range request and returns the
  complete representation, zget safely spools the complete archive to anonymous
  temporary storage and continues through the normal local-file source.
- The earlier ideas for a sequential non-Range parser, `--range-only`, and a
  configurable fallback download limit were explicitly dropped. Do not
  resurrect them without a new decision.

## Product direction

- The technical foundation is mature enough that adoption is constrained more by
  discoverability, installation friction, and proof of value than by missing
  core internals.
- Optimize discover-to-useful-result time to roughly 30 seconds.
- Back performance claims with reproducible measurements and fair comparisons.
- Let real-world usage drive hardening instead of speculative features.

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

## CLI output decisions

- Change ordinary `-o FILE` behavior to match curl-style file output rather than
  preserving the current atomic no-clobber publication semantics.
- Stream extracted bytes directly to the requested output path using normal file
  open semantics. Existing files are truncated/overwritten, and a partial file
  remains if extraction fails after writing has started.
- Treat `-o -` as standard output, as curl does.
- Do not reject existing symlinks, devices, or FIFOs merely because the path
  exists; opening the requested path should follow normal output-file semantics.
- Remove the `lstat`/same-directory `mkstemp`/`fsync`/`link` publication path for
  ordinary `-o` output. The special atomic validated-publication behavior is no
  longer the default contract.
- Update the README and `zget(1)` documentation in the same functional PR as the
  CLI and regression-test changes. Do not land a documentation-only description
  of the old no-clobber behavior first; the open README clarification PR should
  be reworked or superseded by the functional change.

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
