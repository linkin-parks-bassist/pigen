---
status: green
revised_at: "2026-10-11T03:10:50+11:00"
---

The build walk's next in-order step (what/is/the/ordered/output/build/walk/contract.md): when `pigen_build_output_model` reaches a structured node it is required to lower and that node has no published RTL identity, it reports the diagnostic at the node's span and stops.

The two required kinds are a `PIGEN_SYNTAX_MODULE` node (the module) and a `PIGEN_SYNTAX_SIGNAL_DECLARATOR` node (a signal object); each must resolve to a published identity through its semantic record (the record whose `syntax` is the node's index) and then through the RTL record's semantic provenance field (`pigen_rtl_module.semantic_module`, `pigen_rtl_object.semantic_signal`). A node with no matching semantic record, or a semantic record with no matching RTL record, has no published identity.

Reported behavior: `pigen_build_output_model` sets `error->span` to the failing node's `location.source_span`, sets `error->message` to a static string naming the missing lowered identity, returns -1 and stops (no further items emitted after the failure). The 4th argument `pigen_output_error *error` may be NULL — the walk still returns -1. A fully lowered witness walks to 0 with every item emitted and the error left unset. Container kinds (`SIGNAL_DECLARATION`, `CLOCKED_PROCESS`, `PROCEDURAL_BLOCK`, `IF_STATEMENT`, `NONBLOCKING_ASSIGNMENT`, `PARAMETER`, `TYPEDEF`) do not carry a single identity and keep recursing; the diagnostic fires only for the module and the signal declarator.

The diagnostic type is `pigen_output_error { pigen_source_span span; const char *message; }` (span the node's location.source_span, message a borrowed static string — the pigen_syntax_error / pigen_preprocess_error idiom). `pigen_output_error_is_set(const pigen_output_error *)` returns 1 when set (a non-NULL message and a valid span), 0 otherwise (NULL, an unset/all-invalid span); it distinguishes a NULL-argument -1 (no diagnostic) from a real missing-identity diagnostic.

A failure reports the diagnostic and stops, may leave partial state, and no test asserts a failed call left the model unchanged (how/should/compiler/builders/fail.md).
