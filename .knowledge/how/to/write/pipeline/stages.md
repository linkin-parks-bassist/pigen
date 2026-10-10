---
status: green
revised_at: "2026-10-10T18:16:46+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A stage is `stage begin item; end` for one item, or `stage begin items end` for several; an optional name may precede `begin`. There is no colon form. Both forms have one private stage scope and elastic boundary. Lookup is stage-local then pipeline-local then module; shadowing warns. Stage-local wires use combinational = and wire <= is fatal.

Target grammar is pipeline identifier begin declaration* stage+ yield expression; endpipeline. Pipeline declarations are data-type with optional buf and identifier list; every field buf. Stage is optional-name begin one-or-more-items end. Items are declarations or assignments in the currently specified subset. A field <= computes next outgoing field from immutable incoming fields; its value is visible in following stage without an extra cycle beyond the stage boundary. Exactly one final yield reads outgoing state. Nested signal operations/stall are reserved, export is not accepted and no implicit busy exists. Adjacent complete packet bitstreams retain $bits checks; all external consuming inputs participate under full enclosing guard. Stage field liveness pruning remains unspecified; a future optimization must retain uncertain fields conservatively and remain auditable in RTL IR.
