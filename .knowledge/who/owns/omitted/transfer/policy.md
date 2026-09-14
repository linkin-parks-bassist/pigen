---
status: "unverified"
created_at: "2026-09-13T15:03:09+10:00"
scope: "local"
source: "Full 2026-08-27 declaration design/seven-task plan; docs fabric source/SVG; notes invariants; retirement audit Codex /root 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:38:51+10:00"
---

The resolved data-type owner alone decides omission policy. Current Pigen primitive unqualified inputs are abstract. Internal int[n]/uint[n] require explicit realization; bit may use its owned static default. Unqualified integer outputs remain unresolved and require a transfer annotation. Syntax preserves omission and written occurrence separately; declaration resolution does not enumerate primitive families.

pigen_data_type_unqualified_transfer_policy(model,type,is_input) returns STATIC, ABSTRACT or FORBIDDEN. Signed/unsigned Pigen integer and aliases: abstract input, forbidden non-input; bit and aliases: abstract input, supported static non-input; supported ordinary SV: static in either direction. Invalid model/type returns forbidden. Unqualified input bit is an explicit contextual boundary exception despite its shared SV spelling; top-level/static connection specialization remains open, not an excuse to treat every ordinary SV input as dynamic. The general static default is input/inout wire, internal logic, explicitly based output logic and implicit-type output wire; any written wire/reg/logic wins. Dynamic inout diagnoses written occurrence, static inout preserves SV. Explicit input transfer constraints currently use concrete identity pending a separately chosen constraint record. Abstract input body still follows uniform handshake semantics.
