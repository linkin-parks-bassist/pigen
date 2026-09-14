---
status: "unverified"
created_at: "2026-09-13T15:00:33+10:00"
scope: "local"
source: "David's explicit direct linear commit/push instruction, 2026-09-14; current Git history and previous workflow audited"
ingested_by: "Codex /root"
checked_at: "2026-09-14T17:13:16+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T23:16:10+10:00"
updated_by: "opencode"
---

The structured frontend implements source/token provenance and preprocessing, shared type/declaration syntax, scopes and stable symbols, canonical data types/shapes/exact integers, intrinsic two-stage expressions and explicit conversions, lvalues/predicates/clock domains, one signal arena, direct transfers, deduplicated incidence and ownership. Data-first declarations, abstract inputs, omission policy and descriptor-owned FIFO depths resolve through owner APIs.

Elastic RTL Task 1 is implemented. Task 2 is implemented on elastic-rtl-task-2-kt in a44a8b2 with review fixes in 7593604: interned types retain ordered concrete/symbolic packed bounds; typed expressions own ordered children and arbitrary-width signed integer/four-state literal records. The child arena alias use-after-free is corrected by copying inputs before growth. All eleven foundation targets pass, final RTL regression tests pass under AddressSanitizer/UndefinedBehaviorSanitizer, and git diff --check passes. David authorized publishing all current work directly to master in linear history on 2026-09-14; the PR review gate is removed. Subsequent ready/output/emitter/composer work remains unimplemented.

The local tree is the sole project documentation authority; local docs, notes, root Markdown and AGENTS.md are absent. Obsolete history is excluded. The immutable Kestrel reference and global AGENTS.md remain applicable.

Current verification: all eleven structured C foundation targets pass. Full make verify fails when Icarus compiles the pipeline fixture with segmentation fault/exit 139, reproduced by make pipeline-test; the cause remains unresolved in why/does/pipeline/verification/currently/fail.md. No full-suite success is claimed.

The containing personal-project folder was renamed to `Projects`. Git identity and existing local work were preserved. Saved agent directories and snapshot dependencies were migrated; local proofs and Git whitespace checks pass. This rename did not modify compiler implementation or requalify the known pipeline crash.

Reference projects now reside at `~/Projects/reference_projects`. The Kestrel snapshot remains unchanged (all file hashes/modes and Git identity/status checked); Pigen reference knowledge and source/installed CointOS workspace paths were updated. The old home-level path is absent; no compatibility symlink was created.
