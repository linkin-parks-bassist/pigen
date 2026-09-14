---
status: "unverified"
created_at: "2026-09-13T15:00:33+10:00"
scope: "local"
source: "Current observable Qwen session/status and host GPU samples; current-only cleanup commit e73dfee, Codex /root 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-14T17:13:16+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:50:43+10:00"
updated_by: "opencode"
---

The structured frontend implements source/token provenance and preprocessing, shared type/declaration syntax, scopes and stable symbols, canonical data types/shapes/exact integers, intrinsic two-stage expressions and explicit conversions, lvalues/predicates/clock domains, one signal arena, direct transfers, deduplicated incidence and ownership. Data-first declarations, abstract inputs, omission policy and descriptor-owned FIFO depths resolve through owner APIs.

It remains unlinked from ./pigen; production is the quarantined textual prototype. Elastic RTL Task 1 is implemented: rtl.h/rtl.c provide origin-only records, eight distinct IDs, arena ownership and checked accessors. Task 2 types/expressions and subsequent ready/output/emitter/composer work are not implemented. PR 1 is open for Task 1 review; PR 2 covers the knowledge/document cutover. Implementation progress must come from actual code and acceptance evidence.

The local tree is the sole project documentation authority; local docs, notes, root Markdown and AGENTS.md are absent. Obsolete history is excluded. Compiler sources, headers, tests, Makefile, primitives and examples have not changed during this cleanup. The immutable Kestrel reference and global AGENTS.md remain applicable.

Current verification: all eleven structured C foundation targets pass. Full make verify fails when Icarus compiles the pipeline fixture with segmentation fault/exit 139, reproduced by make pipeline-test; the cause remains unresolved in why/does/pipeline/verification/currently/fail.md. No full-suite success is claimed.

Qwen is running in fresh session ses_f60760319ffe426UpRFpjPJAQo on elastic-rtl-task-2-kt, based on current-only cutover e73dfee. Observable view: /tmp/pigen-qwen-current-1789383014/view.json; current pointer: /tmp/pigen-qwen-current-resume. The session reports busy and GPU samples are 71%, 94%, 98%. Its focused kt Task 2 brief requires a failing-test-first implementation milestone; no Task 2 implementation result has been established. Codex /root maintains the current handoff; update status from actual acceptance evidence.
