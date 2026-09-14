---
status: "unverified"
created_at: "2026-09-14T20:38:04+10:00"
scope: "local"
source: "Full intrinsic-expression design/plan 2026-08-25 and semantic invariants; documentation audit Codex /root 2026-09-14"
---

pigen_expr_add_exact_integer(model,value,span) obtains the exact data type. pigen_const_expr_intern_exact_integer(model,value,data_type) requires that type contain the same integer ID. pigen_expr_add_conversion(model,decision,operand,span) and pigen_const_expr_intern_conversion(model,decision,operand) require a nonidentity valid decision, present operand, exact source match and existing target; constructors independently revalidate conversion kind against source/target domains. Constant interning identity includes {kind,source,target,operand}; runtime conversion owns a matching constant conversion exactly when operand is constant. Runtime and constant topology are isomorphic. Shape is preserved, result is target, provenance retained, no separate cast-specific/identity node. A conversion root or converted writable base is a value and cannot be an lvalue. Failure leaves expression/constant-expression counts unchanged. Walkers recurse through a conversion once and record the underlying signal once. Tests build exact3->uint[2]->uint[8], reject identity/source mismatch/invalid target/malformed decisions, preserve shape and reject converted lvalues.
