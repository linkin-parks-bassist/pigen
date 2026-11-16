---
status: green
revised_at: "2026-11-16T15:51:41+11:00"
---

tests/clocked_case_test.py:fsm_checks uses an independent native-SV FSM with a per-invocation live flag. It schedules the state change and clears live on a modeled goto, then gates later authored statements by live. The entered native qualified case completes its own overlap check while later case statements remain suppressed. Functional vectors compare writes before goto, reset suppression, inactive states and transitions; violation modes exercise overlap both in an entered case whose arm jumps and in a later case when no jump executes.

A reference using disable to leave the invocation is unsuitable with installed Verilator 5.020: it lowers disable to a C++ goto before its generated unique-case overlap check, skipping that check. Concrete witness: /tmp/pigen-clocked-case-fsm-native-ipmy39oz/fsm_unique/oracle.sv and native_obj/Vtb___024root__DepSet_hfe20aad3__0.cpp, with branches around lines 158–183 jumping past the report near line 193. The generated Pigen check-only tree correctly reports the entered-case overlap; a disable-based reference instead exits successfully in MODE=1. This is a reference/backend discrepancy, not a reason to suppress the compiler's check.

[IEEE 1800-2017 §12.5.3](https://rfsoc.mit.edu/6S965/_static/F24/documentation/1800-2017.pdf) requires continued item comparison after a match and reporting when more than one item matches, while executing the first matching body's statement. Multiple matching expressions within one comma item do not violate uniqueness. The live-flag reference models the Pigen first-goto contract without the simulator's disable escape path (how/to/write/fsm/states.md). Source selection/check construction belongs to how/are/clocked/case/statements/represented.md. Recheck the backend witness when changing simulator versions.
