---
status: green
revised_at: "2026-10-22T05:30:31+11:00"
---

pigen_data_type_concatenation interns a data type keyed only on the summed packed width of its members, so two concatenations with the same total width but different member orders are the same interned type: any owner-level type-identity or sizing check over a concatenation is order-insensitive. The same class holds for the structured pipeline's outgoing packet, whose aggregate is an unsigned sized vector of the summed packed widths of the yielded fields in source order (src/pipeline_identity.c): the field-to-bit mapping (which yielded field occupies which bits) is unobservable through the data type and through any route-level or owner-level check.

The order law is therefore enforced structurally rather than rejected at the contract level: each concatenation child is written whole, packed bit-stream order left to right, and no contract-level negative witness exists for member order. The order dimension is pinned by emission, per path:

- Route / quarantined-prototype path: pinned today by the emitted SV and the pipeline-test / pipeline-scope-test golden witnesses, which drive the production pigen binary whose pipeline path is the textual prototype (src/pipeline.c).
- Structured pipeline path: no pin exists yet because the structured pipeline is not lowered to SV (src/rtl_lower.c has no PIGEN_SYNTAX_PIPELINE handling). Its owner is the step-(c) parent-module lowering of the pipeline subsystem, whose green manifest and emitted-SV / golden witness pin the established outgoing packet's member order (source order, first yielded field the MSB) once the lowering exists.

A width-correct but misordered aggregate passes every registered structured test today; catching it requires the step-(c) emission witness, not a route-level negative witness.
