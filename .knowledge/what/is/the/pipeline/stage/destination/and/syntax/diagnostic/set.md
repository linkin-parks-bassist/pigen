---
status: green
revised_at: "2026-11-10T12:11:20+11:00"
---

Structured pipeline diagnostics are source-located and distinguish syntax from destination semantics.

- A stage `<=` whose destination is a static wire or module input fails with `wire is not a transfer destination`. The specific message is scoped to pipeline-stage resolution; ordinary non-writable destinations keep `transfer destination is not a writable variable or signal`.
- A first-stage read of the field being written fails as `buffered transfer cannot source its destination`.
- `yield` inside a stage fails with `yield is only after the final stage`.
- `export` in a pipeline body fails with `export is reserved but not yet supported`.
- Duplicate pipeline field declarations fail with `duplicate pipeline field name`.
- A non-buffered pipeline field fails with `pipeline field transfer type must be buf`.
- An `always_ff` event control that is not exactly one `posedge` fails with `exactly one posedge event control`.
- A non-clocked `always` block is retained verbatim; failed edge-clocked blocks roll back.

Stage-local `wire` declarations are not accepted by the structured stage-item parser, so a fixture using one stops at `unrecognized pipeline stage item` before assignment lowering.
