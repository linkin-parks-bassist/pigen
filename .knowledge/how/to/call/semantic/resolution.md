---
status: "unverified"
created_at: "2026-09-13T15:04:52+10:00"
scope: "local"
source: "include/pigen/resolve.h; include/pigen/resolve_policy.h; include/pigen/expression_resolve.h"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:22+10:00"
---

pigen_resolve_semantics consumes a syntax tree, destination semantic model, positive pigen_resolve_policy maximum_generated_bits and optional semantic error. pigen_resolve_expression resolves values, pigen_resolve_constant_expression uses the same walker but requires a constant identity with explicit literal domain, and pigen_resolve_assignment_value accepts target data type for the final boundary conversion. These are library APIs, not production CLI options.
