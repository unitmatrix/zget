# Project working context

This file is a compact continuity note for future work on zget. It is not
product documentation and is intentionally kept only on the `assistant/context`
branch.

Update it when a decision would be costly to reconstruct from chat history:
architecture choices, explicitly rejected approaches, release state, or a
change in the immediate next task. Do not turn it into a running chat
transcript, and do not duplicate information that already has an authoritative
home elsewhere in the repository.

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
  resurrect them as pending work without a new decision.
- Installation guidance is now on `main` via merged PR #31. It keeps the general
  path short: install runtime dependencies first, then download the release
  archive, extract it, and place `zget` on `PATH`; the concrete copy-paste
  example is Ubuntu 24.04 x86_64 and uses an `X.Y.Z` version placeholder.
- For distribution, keep the current versioned release tarballs for now. Do not
  add installer scripts, new release asset variants/aliases, Homebrew tap repos,
  or distro packages unless that decision is explicitly revisited.
- Immediate next product work is reproducible benchmarking. Do not start
  `--stat` before that benchmark step is completed or reprioritized.

## Product direction

- The technical foundation is mature enough that adoption is now constrained
  more by discoverability, installation friction, and proof of value than by
  missing core internals.
- Optimize the path from discovering zget to getting useful results in roughly
  30 seconds: easy installation, a very clear value proposition, and a concrete
  example.
- Back performance claims with reproducible measurements: bytes transferred,
  latency/time to first result, total time, and memory on representative large
  archives, with fair comparisons where workloads are equivalent.
- Treat `--stat`, `--exists`, and machine-readable output as important Unix and
  automation building blocks, not merely convenience features.
- Seek real-world usage across large hosted ZIPs, datasets, release/build
  artifacts, firmware, and CDN/object-storage workloads; use that feedback to
  drive hardening rather than adding speculative features.
- Product/adoption priorities belong in the authoritative roadmap, not here;
  this section records the reasoning behind those priorities.

## Authoritative project sources

- Roadmap: `ROADMAP.md` on the `docs/roadmap` branch.
- Release version: `VERSION` on the release branch / `main`.
- User-facing behavior and installation guidance: normal project documentation
  on `main`.

## Working conventions

- Prefer small, focused PRs with one coherent purpose.
- Treat CI failures as evidence to diagnose from the failing job/test/log before
  changing code.
- Add focused regression coverage with behavior changes.
- Keep user-facing documentation synchronized with behavior.
- Avoid speculative refactors and duplicated sources of truth.
- Before tagging a release: docs/readiness review, version bump through
  `VERSION`, green CI, then tag the exact green release commit.

## Continuity rule

At the start of future zget work, read this file and the current
`docs/roadmap:ROADMAP.md` before reconstructing project decisions from memory.
When a new important decision is made during a session, proactively suggest
updating this file; if the user agrees, update it immediately.
