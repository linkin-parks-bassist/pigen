---
status: green
revised_at: "2026-11-15T09:34:10+11:00"
---

tests/pipeline_output_test.c pins the module-scope pipeline identifier contract through include/pigen/pipeline_identity.h and src/pipeline_identity.c. make pipeline-output-test runs four sections, also registered in tests/contracts.json. The witness builds semantic module/scope/clock/process records directly to exercise this API independently of production resolution. Production resolves inline pipelines through src/resolve.c; it does not reject them merely for occurring in a clocked process.

The module-scope pigen_pipeline_identifier_establish/of entries declare and recover one BUF output signal bound to a module-scope symbol. Its spans anchor at the pipeline identifier token. The provisional packet type is an unsigned vector whose width sums the declared fields read by the final yield. The production resolver subsequently resolves the actual yield type and makes the output internal. Scoped establish_in_scope/of_in_scope variants support module-descendant scopes; nested identities are internal. This four-section manifest retains the module-scope compatibility contract.

- output-identity checks one 8-bit yielded field and requires both BUF transfer type and OUTPUT direction.
- outgoing-packet-type checks widths 8 for one field, 24 for two yielded fields, 8 for a proper prefix subset and 16 for a non-prefix subset yielding only the second field. The last witness distinguishes name-based field membership from selecting the first N declarations. Summing every declaration would incorrectly give 24 in both subset cases; po_outgoing_width is deliberately not the subset oracle.
- module-scope-binding requires the symbol in the module scope, recoverable through pigen_symbol_signal and the module signal set, with symbol/signal spans at the identifier.
- buf-field-consistency checks omitted BUF and mixed omitted/explicit BUF declarations. Every pipeline field has BUF transfer type.

The manifest checks field-set membership and packed width, not member order: width-keyed aggregate types cannot distinguish concatenation order. Emitted-SV/golden witnesses own that distinction (does/the/concatenation/data/type/keying/keep/member/order.md). Existing pipeline-identity-test covers shape, field BUF policy, scope and yield. Generated-scope behavior needs separate resolver and hardware witnesses; the module-scope unit checks do not establish it.

The identity builder's initial width comes from declaration spelling; production's resolved-yield type supersedes that provisional value. Parameterized field typing belongs to the normal type/expression owners. Failure stops compilation and may leave partial model state (how/should/compiler/builders/fail.md).
