---
status: green
revised_at: "2026-11-11T17:08:36+11:00"
---

Structured primitive emission is owned by RTL records and the terminal backend. pigen_rtl_instance carries its primitive enum, payload_type, parameter expression range, connection records, owner and provenance. Final instance names come from pigen_rtl_assign_names.

src/sv_emit.c maps the primitive enum to the corresponding pigen primitive definition. It emits PAYLOAD_T from the resolved RTL type, FIFO depth as DEPTH and merge source count as N. Connection records carry port names and expressions/objects. This backend mapping does not consult prototype descriptors, semantic symbols or generated text.

Legacy instances without a primitive enum retain the positional parameter-value and object-connection rendering used by record-level emitter tests. Their limited golden form is not a statement that production primitives lack module or parameter names.

The emitter's primitive payload type spelling preserves RTL packed dimensions, including direction and multiple dimensions, consistently with object declarations. The backend consumes these already-lowered bounds rather than reconstructing a semantic type.

Evidence: include/pigen/rtl.h, src/rtl.c, src/rtl_lower.c, src/sv_emit.c; make rtl-test, sv-emit-test and the primitive simulation gates in make verify. The full terminal rendering contract is what/is/the/terminal/systemverilog/emitter/contract.md.
