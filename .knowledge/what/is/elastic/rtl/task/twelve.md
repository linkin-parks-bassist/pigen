---
status: green
revised_at: "2026-11-09T22:34:26+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured owner suite is green for: source, preprocess, transfer-type, syntax-model, fsm-syntax, integer, semantic, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, output-model, route (all four), pipeline-output/stage/lower, fsm-lower and compile. The production binary lowers core datapath, inline pipelines and FSMs to synthesizable SV. Behavioural simulations pass for a three-stage buf chain (12 ordered packets), a three-stage arithmetic pipeline with backpressure, and an FSM counter.

Still red and awaiting rebaseline: nine rtl-lower sections and two sv-emit sections pin the superseded contract (invalid storage controls, update value equal to fire, positional instance rendering, OPAQUE must cover structured spans). Corpus targets remain red because they use prototype dialect or unimplemented child-instance/fabric/validity-intrinsic features.

No quarantine violations in the structured lowering/emitter: no source-text searches in sv_emit.c and no concrete transfer enums in the RTL lowering.
