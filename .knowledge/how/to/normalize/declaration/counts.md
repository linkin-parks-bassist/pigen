---
status: green
revised_at: "2026-11-11T17:18:25+11:00"
---

pigen_const_expr_normalize_count(model,value) owns type counts and transfer depths. Known positive ordinary/exact integers and known packed-bit values normalize into canonical unsized positive constants; symbolic integral constants remain structural for later elaboration. Zero, signed negatives, X/Z, values beyond uint64 and invalid IDs fail closed. int[0] retains the type count must be a positive integer diagnostic.

pigen_const_expr_evaluate_u64 decodes known literal bit patterns and concatenates fields in packed order using their declared widths. A signed negative literal is rejected as a standalone count, but its raw bits remain valid inside an unsigned concatenation. Leading zero bits may exceed 64 bits when the numerical result fits. Binary/conversion/conditional results respect resolved packed width/signedness; arithmetic widening belongs to the type owner. Evaluator records are snapshotted and arena pointers refreshed across width queries that may intern constants.

Conditional counts evaluate a known condition (including signed comparisons), then only the selected branch. A negative or invalid selected result still fails count validation. Signed packed coordinates are distinct from counts. Range-width evaluation uses pigen_const_expr_evaluate_i64 for signed endpoints and computes the absolute distance plus one with overflow checks, including ranges crossing zero. A negative coordinate does not authorize a negative width/depth. The signed evaluator and coordinate lowering contract are in what/is/elastic/rtl/task/five.md.

FIFO resolution switches on descriptor.parameter NONE/DEPTH: NONE requires an absent argument; DEPTH resolves a constant in PIGEN_LITERAL_DOMAIN_PIGEN, validates it with the normalizer and retains its semantic expression ID. Width, depth and declarator shape remain independent. Symbols and ordinary ranges retain their SV literal domain.

The directionless-port scanner ignores commas nested in parentheses/brackets/braces, allowing marker-free static bit[{...}] declarations to reach count validation. child-port-endpoint-test simulates parameterized packed counts/depths and signed coordinate aliases. packed-count-error-test covers static width, zero, negative, unknown and truncating-shift-zero diagnostics. semantic-test covers packed ordering, signed fields, signed coordinates and overflow. General constant-expression coverage remains subject to acceptance audit; these tests do not establish every cast/operator combination.
