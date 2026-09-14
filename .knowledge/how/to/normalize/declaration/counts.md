---
status: "unverified"
created_at: "2026-09-14T20:38:51+10:00"
scope: "local"
source: "Full 2026-08-27 declaration design/seven-task plan; docs fabric source/SVG; notes invariants; retirement audit Codex /root 2026-09-14"
---

pigen_const_expr_normalize_count(model,value) is the single semantic constant owner for type counts and transfer depths. Known positive ordinary/exact integers normalize into canonical unsized positive constants; symbolic integral constants remain structural for later elaboration. Known zero/negative/nonintegral/over-wide values and invalid model/ID fail closed. int[0] retains the type count must be a positive integer diagnostic at the argument. FIFO argument resolution switches on descriptor.parameter NONE/DEPTH rather than concrete FIFO: NONE requires invalid argument as valid absence; DEPTH uses pigen_resolve_constant_expression in explicit PIGEN_LITERAL_DOMAIN_PIGEN, obtains constant ID, validates with the normalizer and retains the semantic expression ID as generic transfer argument. Depth/width/shape are independent. Symbols/ordinary ranges retain their own SV literal domain.
