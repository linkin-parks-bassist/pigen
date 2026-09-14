---
status: "unverified"
created_at: "2026-09-14T20:37:11+10:00"
scope: "local"
source: "David's current-only knowledge instruction 2026-09-14; audited current contracts and inspected owner interfaces; Codex /root"
updated_at: "2026-09-14T20:48:47+10:00"
---

pigen_const_expr_intern_width_maximum(model, values, count) uses the shared canonical sequence interner with PIGEN_CONST_EXPR_WIDTH_MAXIMUM. Flatten nested maxima, deduplicate identities, collapse literal terms to the greatest literal, remove zero when another term exists, sort remaining identities, return one remaining child directly. Reject zero children and invalid children. Maximum is commutative, associative and idempotent; sums/products are commutative/associative but preserve multiplicity. Tests use max(8,16,8)=16, symbolic select-width plus 8 reordered to the same ID, nested/repeated/single/zero/invalid cases. The owner files are semantic.h/c and semantic_test.c; check semantic-test, expression-resolve-test and resolve-test. Arithmetic sizing is intrinsic and range-derived.
