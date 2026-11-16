---
status: green
revised_at: "2026-11-16T09:10:37+11:00"
---

The nearer_import case in tests/native_protocol_test.py:retained_namespace_tests uses Icarus 13 for value simulation because installed Verilator 5.020 produces different values from the specified lexical binding. Both simulators still receive ordinary SV; warnings-fatal Verilator lint remains enabled.

The reproducer has origin::X=7, other::X=13 and third::X=17. relay imports origin::* and defines a function with local X=99 whose nested block imports other::* and returns X. After the function, relay explicitly imports third::X, assigns localparam N=X and exports its imports. IEEE 1800-2017 §26.3 searches each lexical scope's visible declarations/imports and then its wildcard candidates before moving outward. The nested return binds other::X=13; N binds third::X=17. The primary rule is in the [IEEE standard](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf).

An independently compiled original-SV reproducer yields N=17 and function=13 with Icarus, but N=7 and function=99 with Verilator. The regression therefore compares original and emitted SV values using Icarus for this case. Other retained-body forms use Verilator when Icarus cannot elaborate them. Do not change compiler lookup or expected values to match the observed Verilator result. Recheck this bounded exception when the installed backend changes; the internal simulator cause has not been established.
