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

## Resource getter decisions

- Position zget as an archive-aware file getter: get a complete HTTP(S)
  resource, or get one selected file inside a supported remote container.
- Change the CLI synopsis to `zget [-o FILE] URL [MEMBER]`. With no `MEMBER`,
  stream the complete representation directly without archive parsing. With a
  `MEMBER`, retain the current selective Range path and safe full-download
  fallback.
- Give the public one-shot `zget_get()` call the same optional-selector
  contract: a NULL member streams the complete resource, a nonempty member
  selects that archive member, and an empty string remains invalid.
- Keep the reusable context API archive-oriented. `zget_open_url()` continues to
  open a supported archive, `zget_extract_member(ctx, NULL, ...)` remains
  invalid, and `zget_list(ctx, NULL, ...)` continues to list every member.
- Treat the `zget_get(NULL member)` behavior as a compatible input-domain
  expansion but a deliberate semantic API change. Do not change its signature,
  public structs, or result-code ABI. Ship the new contract in 0.6.0, whose
  normal pre-1.0 version policy also establishes the 0.6 shared-library line.
- Reuse one private callback-based complete-HTTP-transfer primitive. Adapt it to
  anonymous temporary storage for archive fallback and directly to the CLI
  output sink for whole-resource retrieval; do not expose a separate generic
  download API merely to share the implementation.
- Keep whole-resource retrieval deliberately narrow: HTTP(S) GET, existing
  redirect and transport security policy, stdout or explicit `-o`, and existing
  partial-output semantics. Do not grow zget into a general curl replacement or
  infer output filenames automatically.
- Describe ZIP as the first supported container rather than the product
  identity. Add formats only when selective remote access offers a demonstrated
  advantage; do not add a speculative format/plugin framework.

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
