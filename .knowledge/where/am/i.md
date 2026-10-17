---
status: green
revised_at: "2026-10-15T07:48:24+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

The structured frontend resolves data-first declarations and intrinsic semantic types/expressions, but remains unlinked from ./pigen. Production is the quarantined textual prototype. Elastic RTL Tasks 1–9 are implemented and complete: IDs/arena, canonical types/expressions, owner-validated hardware constructors and collision-safe names, type/expression adapters, declaration lowering, atomic transfers sharing one fire, and the ordered output model. Tasks 10 (terminal SV emission), 11 (quarantined composition) and 12 (verification) are complete on master; the remaining in-order work is the step-5 repair in what/is/the/plan.md. Failure rollback is not a requirement: compiler stages report their diagnostic and stop (how/should/compiler/builders/fail.md). No partial frontend production attachment, fallback, second dialect or per-file choice is approved.

Repository topology: src/ owner subsystems and production prototype; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ executable prototype designs/testbenches; .knowledge/ the sole local documentation and architectural answer authority. Local docs, notes, root Markdown and AGENTS.md are absent. Keep only current contracts, implementation evidence and actionable unresolved questions; delete obsolete material. Detailed semantic coverage is in what/is/the/knowledge/ingestion/coverage.md.

- how/ answers mechanisms/procedures: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/contracts/current work: what/is/a/signal.md, what/is/the/spec.md, what/is/the/plan.md, what/is/elastic/rtl/task/five.md.
- where/ locates code/evidence: where/are/compiler/owner/interfaces.md, where/is/the/human/written/style/reference.md.
- why/ gives architecture rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior/presence: does/the/structured/frontend/run/in/production.md, does/a/payload/projection/consume/the/whole/signal.md.
- is/ answers classification/status: is/ingress/a/shared/transfer/type.md, is/pigen/division/specified.md.
- when/ gives decision gates: when/can/production/switch/to/the/structured/compiler.md, when/to/ask/david/about/architecture.md.
- who/ gives catalogue ownership: who/owns/omitted/transfer/policy.md, who/owns/compiler/catalogues.md.

This local answer tree is the project's maintained knowledge source; use this orientation and what/is/the/plan.md for implementation progress, never infer it from future contracts.

The structured frontend implements source/token provenance and preprocessing, shared type/declaration syntax, scopes and stable symbols, canonical data types/shapes/exact integers, intrinsic two-stage expressions and explicit conversions, lvalues/predicates/clock domains, one signal arena, direct transfers, deduplicated incidence and ownership. Data-first declarations, abstract inputs, omission policy and descriptor-owned FIFO depths resolve through owner APIs. Ordinary branches and worktrees may be used for isolation and concurrent work; pigen does not require direct-to-master or linear history.

Current verification: all eleven structured C foundation targets pass. rtl-lower-test compiles clean under -Werror; the full Task 8 manifest and the Task 9 sections pass, the manifest one entry per present section, every command green on master. rtl-test, rtl-name-test, transfer-type-test and resolve-test are green. Icarus Verilog 13.0 is installed at ~/.local/bin (the Ubuntu 12.0 package mis-elaborates `$bits(<signal>)` widths; why/does/pipeline/verification/currently/fail.md); Verilator 5.020 is installed. On the accepted main, full `make verify` currently fails because coslice-test hits the conditional co-sliced transfer breakage in what/is/broken.md, and its lint does not yet fail on warnings. The local tree is the sole project documentation authority; obsolete history is excluded. The immutable Kestrel reference remains applicable.
