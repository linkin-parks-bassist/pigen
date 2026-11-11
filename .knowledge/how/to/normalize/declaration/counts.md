---
status: green
revised_at: "2026-11-11T16:21:16+11:00"
---

pigen_const_expr_normalize_count(model,value) owns type counts and transfer depths. Known positive ordinary/exact integers and known packed-bit values normalize into canonical unsized positive constants; symbolic integral constants remain structural for later elaboration. Zero, signed negatives, X/Z, values beyond uint64 and invalid IDs fail closed. int[0] retains the type count must be a positive integer diagnostic.

pigen_const_expr_evaluate_u64 decodes known literal bit patterns and concatenates fields in packed order using their declared widths. A signed literal is rejected as a negative standalone count when its sign bit is set, but its raw bits remain valid inside an unsigned concatenation. Leading zero bits may exceed 64 bits when the numerical result fits. Binary/conversion results respect their resolved packed width and signedness; Pigen arithmetic widening remains determined by the type owner. Evaluator records are snapshotted and arena pointers refreshed across width queries that may intern new constants.

FIFO argument resolution switches on descriptor.parameter NONE/DEPTH: NONE requires an absent argument; DEPTH resolves a constant in PIGEN_LITERAL_DOMAIN_PIGEN, validates it with the normalizer and retains the semantic expression ID. Width, depth and declarator shape remain independent. Symbols and ordinary ranges retain their own SV literal domain.

The directionless-port fallback scanner ignores commas nested in parentheses, brackets and braces, allowing marker-free static bit[{...}] declarations to reach count validation. Parameterized widths and FIFO depths using packed concatenations are simulated in child-port-endpoint-test. packed-count-error-test covers a static-width simulation and zero, negative, unknown and truncating-shift-zero diagnostics. semantic-test covers bit patterns, concatenation ordering, signed fields and evaluator-range rejection. General constant-expression coverage remains subject to the language acceptance audit; these tests do not establish every cast/operator combination.
