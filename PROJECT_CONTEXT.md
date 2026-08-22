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
