---
status: "unverified"
created_at: "2026-09-13T15:03:12+10:00"
scope: "local"
source: "SPEC/PLAN/architecture/semantic invariants/Ari/editorial notes linear retirement audit; David knowledge-tree-only instruction 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:39:58+10:00"
---

A single item can use `stage: field <= expression;` or a name before the colon. Multiple items use `stage begin ... end`. Both forms have one private stage scope and elastic boundary. Lookup is stage-local then pipeline-local then module; shadowing warns. Stage-local wires use combinational = and wire <= is fatal.

Target grammar is pipeline identifier begin declaration* stage+ yield expression; endpipeline. Pipeline declarations are data-type with optional buf and identifier list; every field buf. Stage is optional-name colon one item, or optional-name begin items end. Items are declarations or assignments in the currently specified subset. A field <= computes next outgoing field from immutable incoming fields; its value is visible in following stage without an extra cycle beyond the stage boundary. Exactly one final yield reads outgoing state. Nested signal operations/stall are reserved, export is not accepted and no implicit busy exists. Adjacent complete packet bitstreams retain $bits checks; all external consuming inputs participate under full enclosing guard. Stage field liveness pruning remains unspecified; a future optimization must retain uncertain fields conservatively and remain auditable in RTL IR.
