---
status: green
revised_at: "2026-10-03T01:33:46+10:00"
---

The Task 6 implementation chain (impl-boundary, impl-net-variable, impl-storage) is staged to turn tests/rtl_lower_test.c green one realization family at a time, but the implementation landing gate cannot accept an intermediate commit that leaves that suite red.

Root cause: CointOS's landing gate (what/is/cointos.md, runtime tree) requires, for an implementation commit, that "every accepted test contract covering changed code must pass; unrelated red tests may remain." tests/contracts.json registers `make rtl-lower-test` covering `src/rtl_lower.c` (the only file each implementation item changes), so the gate runs that whole-binary suite and requires it to PASS. The suite is a single assert-aborting process with no section-scoped sub-target: it runs sections in file order and stops at the first failing assert.

The `Expected red: COMMAND => OUTPUT` mechanism — which lets the landing gate *require* a named command to fail with a literal output — is declared by **test-contract** reports only. That is how all of sections (8)-(15) landed red (archive refs refs/cointos/archive/work/elastic-rtl-task-6-test-contract-*). Implementation reports carry only `Status: done|blocked` and have no expected-red accounting, so an implementation commit is held to the full pass of its covering contract.

Consequence for the chain:
- impl-boundary (BOUNDARY): section (8) green, section (9) red → suite aborts at tests/rtl_lower_test.c:1915 → gate rejects.
- impl-net-variable (net/variable): sections (8)(9) green, section (10) red → suite aborts at section (10)'s first assert → gate rejects.
- impl-storage (storage): sections (8)(9)(10) green → suite fully green → gate accepts.
- final-green (integration): verifies the full-green gate.

So impl-boundary and impl-net-variable cannot land as scoped while the staged reds are active; this is a structural policy gap (no staged-red/expected-red accommodation for implementation items), not a code defect. The verified worker implementation 617b933 (work/elastic-rtl-task-6-impl-boundary) satisfies every brief acceptance criterion: section (8) PASS, suite aborts exactly at 1915 (section (9)'s first deliberate red), make rtl-test / rtl-name-test / transfer-type-test green, no PIGEN_TRANSFER_TYPE_*(ABSTRACT|WIRE|REG|LOGIC|BUF|PORT|FIFO|SKID) reference in src/rtl_lower.c, git diff --check clean. It is preserved on its branch and must not be re-sent as an unchanged retry.

Unresolved (needs a user/CointOS policy decision before the chain can proceed): how the implementation landing path should account for a staged red in a covering whole-binary suite. Options on the table: (a) a CointOS gate/policy change to let implementation reports declare an expected red (mirroring test-contract); (b) a project test-policy change so each realization family has its own section-scoped runnable target that passes in isolation; (c) a one-time authorized landing of the verified chain commits. This is the specific blocker for the Task 6 implementation chain; do not queue another implementation retry around it.
