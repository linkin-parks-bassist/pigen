---
status: green
revised_at: "2026-11-11T16:09:32+11:00"
---

pigen_const_expr_normalize_count(model,value) is the semantic constant owner for type counts and transfer depths. Known positive ordinary/exact integers normalize into canonical unsized positive constants; symbolic integral constants remain structural for later elaboration. Known zero/negative/nonintegral/over-wide values and invalid model/ID fail closed. int[0] retains the type count must be a positive integer diagnostic at the argument.

FIFO argument resolution switches on descriptor.parameter NONE/DEPTH rather than concrete FIFO: NONE requires invalid argument as valid absence; DEPTH uses pigen_resolve_constant_expression in explicit PIGEN_LITERAL_DOMAIN_PIGEN, obtains constant ID, validates with the normalizer and retains the semantic expression ID as generic transfer argument. Depth/width/shape are independent. Symbols/ordinary ranges retain their own SV literal domain.

Current limitation: a known packed concatenation such as `{4'd0,4'd8}` is not evaluated by pigen_const_expr_evaluate_u64 and is rejected as a type count, including through a parameter override. The same override parses and works as a payload mask in tests/child_port_expression_parameter_concat.pigen. Packed-bit count acceptance needs an implementation/compatibility audit; the rejection is not evidence that the value is nonpositive.
