---
status: green
revised_at: "2026-10-07T23:05:33+11:00"
---

The Task 8 lowering's owner API surface (include/pigen/semantic.h, predicate.h, rtl.h, rtl_lower.h). Transfers: `pigen_transfer_get`, `pigen_transfer_signal_uses`, `pigen_transfer_add`. Predicates: `pigen_predicate_get`, `pigen_predicate_atoms`, `pigen_predicate_and_condition`, `pigen_predicate_true/false`, `pigen_predicates_mutually_exclusive`. Lvalues: `pigen_lvalue_get`, `pigen_lvalue_children`. RTL owners: `pigen_rtl_module_add_with_owner`, `pigen_rtl_equation_add_with_owner`, `pigen_rtl_update_add_with_owner`, `pigen_rtl_process_add_with_owner`, `pigen_rtl_module_get`. Lowering: `pigen_rtl_lowering_init`, `pigen_lower_rtl_type`, `pigen_lower_rtl_expression`, `pigen_lower_rtl_module_declarations`.
