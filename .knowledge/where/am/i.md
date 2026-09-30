---
status: green
revised_at: "2026-10-01T07:51:04+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

The structured frontend resolves data-first declarations and intrinsic semantic types/expressions, but remains unlinked from ./pigen. Production is the quarantined textual prototype. Elastic RTL Tasks 1-4 are implemented and landed: Task 1 IDs/arena, Task 2 canonical types/expressions, Task 3 resolved-hardware constructors with owner validation, and Task 4 collision-safe names (checked SOURCE spans copied into model-owned text, unchecked spans failing the whole assignment with no partial names, internal stems suffixed only on collision). Task 5 owner-based type/expression adapters is in flight: its skeleton and its full five-item test-contract chain (types, expressions, constant-identity + content, rollback, memo-stability) are all landed. The implementation chain so far has only two landed/failed items - impl-type and impl-expression - both RETAINED FAILED with their halves preserved on durable branches (type-half 022d738, expression-half 108a24f), each blocked only on a frozen-test defect now corrected by a separate test-contract item: the s4 type-map guard fix landed as 7c95391, the section 5 expression-map guard fix (elastic-rtl-task-5-test-contract-fix-s5-expr-map-guard) LANDED at 322e3f2, and the section 6 unbound-witness restructure (elastic-rtl-task-5-test-contract-fix-s6-unbound-witness) is still in flight. impl-expression's dependents are repointed onto both fixes (cointos replace, decompose-elastic-rtl-task-5-impl-expression). The planned impl-constant / impl-error-memo / impl-final-green children are NOT yet in the queue (a planning gap to close before the chain can continue past impl-expression). See what/is/the/plan.md and what/is/elastic/rtl/task/five.md. No partial frontend production attachment, fallback, second dialect or per-file choice is approved.

Repository topology: src/ owner subsystems and production prototype; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ executable prototype designs/testbenches; .knowledge/ the sole local documentation and architectural answer authority. Local docs, notes, root Markdown and AGENTS.md are absent. Keep only current contracts, implementation evidence and actionable unresolved questions; delete obsolete material. Detailed semantic coverage is in what/is/the/knowledge/ingestion/coverage.md.

- how/ answers mechanisms/procedures: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/contracts/current work: what/is/a/signal.md, what/is/the/spec.md, what/is/the/plan.md, what/is/elastic/rtl/task/five.md.
- where/ locates code/evidence: where/are/compiler/owner/interfaces.md, where/is/the/human/written/style/reference.md.
- why/ gives architecture rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior/presence: does/the/structured/frontend/run/in/production.md, does/a/payload/projection/consume/the/whole/signal.md.
- is/ answers classification/status: is/ingress/a/shared/transfer/type.md, is/pigen/division/specified.md.
- when/ gives decision gates: when/can/production/switch/to/the/structured/compiler.md, when/to/ask/david/about/architecture.md.
- who/ gives catalogue ownership: who/owns/omitted/transfer/policy.md, who/owns/compiler/catalogues.md.

This local answer tree is maintained by the project's agents; use this orientation and what/is/the/plan.md for implementation progress, never infer it from future contracts.

The structured frontend implements source/token provenance and preprocessing, shared type/declaration syntax, scopes and stable symbols, canonical data types/shapes/exact integers, intrinsic two-stage expressions and explicit conversions, lvalues/predicates/clock domains, one signal arena, direct transfers, deduplicated incidence and ownership. Data-first declarations, abstract inputs, omission policy and descriptor-owned FIFO depths resolve through owner APIs.

Elastic RTL Task 2 is implemented and landed: interned types retain ordered concrete/symbolic packed bounds; typed expressions own ordered children and arbitrary-width signed integer/four-state literal records. The child arena alias use-after-free is corrected by copying inputs before growth. All eleven foundation targets pass, final RTL regression tests pass under AddressSanitizer/UndefinedBehaviorSanitizer, and git diff --check passes. Ordinary branches, worktrees and merges may be used for isolation and concurrent work; pigen does not require direct-to-master or linear history. Subsequent ready/output/emitter/composer work remains unimplemented.

The local tree is the sole project documentation authority; obsolete history is excluded. The immutable Kestrel reference remains applicable.

Current verification: all eleven structured C foundation targets pass. Full make verify fails when Icarus compiles the pipeline fixture with segmentation fault/exit 139 (re-confirmed 2026-09-29 on Icarus Verilog 12.0 via make pipeline-test); the cause remains unresolved in why/does/pipeline/verification/currently/fail.md. No full-suite success is claimed.

The containing personal-project folder was renamed to `Projects`. Git identity and existing local work were preserved. Saved agent directories and snapshot dependencies were migrated; local proofs and Git whitespace checks pass. This rename did not modify compiler implementation or requalify the known pipeline crash.

Reference projects now reside at `~/Projects/reference_projects`. The Kestrel snapshot remains unchanged (all file hashes/modes and Git identity/status checked); Pigen reference knowledge and source/installed CointOS workspace paths were updated. The old home-level path is absent; no compatibility symlink was created.
