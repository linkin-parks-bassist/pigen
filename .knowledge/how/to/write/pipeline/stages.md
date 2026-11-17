---
status: green
revised_at: "2026-11-17T11:53:11+11:00"
checked_at: "2026-10-15T09:22:25+11:00"
---

A stage is `stage begin item; end` for one item, or `stage begin items end` for several; an optional name may precede `begin`. There is no colon form. Both forms have one private stage scope and elastic boundary. Lookup is stage-local, incoming field-version scope, then enclosing scope. Stage locals cannot shadow pipeline fields. Stage-local wire initializers use combinational = and contribute their dependencies to the stage event; <= destinations must be complete pipeline fields.

The defining language model reuses one field name across stages: x denotes incoming state on reads and outgoing state on the left of <=. Each boundary represents separate physical storage. Production implements separate stage versions and atomic packet transfers; how/do/pipeline/stage/reads/work.md owns the mechanism and independent witnesses.

Target grammar is pipeline identifier begin declaration* stage+ yield expression; endpipeline. Pipeline declarations are data-type with optional buf and one identifier per declaration; every field buf. Stage is optional-name begin one-or-more-items end. Items are declarations or assignments in the currently specified subset. A field <= computes next outgoing field from immutable incoming fields; its value is visible in following stage without an extra cycle beyond the stage boundary. Exactly one final yield reads outgoing state. Nested signal operations/stall are reserved, export is not accepted and no implicit busy exists. Unwritten initialized fields carry forward, and all incoming packet fields and external consuming inputs participate in one stage event under the complete enclosing guard. Stage field liveness pruning remains unspecified; a future optimization must retain uncertain fields conservatively and remain auditable in RTL IR.
