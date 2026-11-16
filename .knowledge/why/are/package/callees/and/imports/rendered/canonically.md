---
status: green
revised_at: "2026-11-16T10:14:58+11:00"
---

Canonical rendering preserves the compiler's selected routine identity across backend namespace lookup differences. Semantic construction chooses the original package/function or task; output preparation spells that identity explicitly and establishes actual package routine imports before candidate wildcards. The implementation contract is how/are/retained/package/calls/bound.md.

IEEE 1800-2017 §26.3 gives calls scope-end visibility for locally visible names while restricting wildcard candidates to the call's source point ([standard](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf)). A checked ordinary-SV reproducer imports first_calls::*, defines f() calling g(), then imports later_calls::g. Icarus 13 and Verilator 5.020 both returned the earlier function's value. Qualifying the selected later callee produced the intended result. The forward_package_call_tests qualified-reference comparisons cover this choice over varying inputs.

Verilator also selected the earlier function when a downstream module imported the wildcard-exported name. Qualifying the body call alone, or replacing export *::* with explicit export directives, did not fix that observation. Preparing the actual canonical routine import immediately after the package header did. This preparation applies to routine names; DATA/constant imports retain their source-point handling.

Qualified re-exported function access is a separate Icarus limitation. A plain-SV package importing/exporting origin::compute rejects public_functions::compute(3), while importing public_functions::compute into the caller and calling compute(3) returns 16. The new native pin benches therefore check the exported function through an explicit import. forward_package_call_tests:qualified_export retains the qualified spelling and verifies its values with Verilator. Local forward qualified routines, named arguments and qualified task forms also use the backend that accepts those SV forms; warnings-fatal lint remains enabled.

These are bounded backend observations, not language restrictions. Recheck them when simulator versions change. Keep independent qualified references and both export access forms in verification; do not alter expected semantic values merely to match backend lookup. Source evidence: tests/native_protocol_test.py:forward_package_call_tests and forward_call profiles in native_protocol_test.py/native_array_test.py.
