---
status: green
revised_at: "2026-11-11T16:06:22+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A domain is canonical by resolved clock declaration identity and event edge. Each clocked process retains separate syntax identity, clock-expression occurrence, module and provenance even if it shares that domain. Pigen actions require a synchronous one-edge always/always_ff; clocked Pigen use binds a signal and later clocked uses must match.

Computed elastic child-input connections use structural processes with invalid clock/domain IDs, created through pigen_structural_process_add. Their atomic transfers reuse expression-use, acceptance and ownership analysis without binding a clock domain. Lowering emits combinational payload/valid/ready equations and rejects sequential updates in a structural process. No RTL clocked process or storage is introduced. Cross-instance clock-domain compatibility remains unaudited; this mechanism does not establish that connected children share a clock.
