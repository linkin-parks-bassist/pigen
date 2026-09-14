---
status: "unverified"
created_at: "2026-09-13T15:05:52+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:21+10:00"
---

Verification and durable boundary. Run clean structured C/backend/vertical-slice suite, unchanged make verify and side-channel/source-enum/quarantine checks. Investigate tracked generated changes; expected production effect is none. Update PLAN/architecture/invariants only for implemented RTL IR and tested narrow unlinked adapter. Leave production integration/deletion, full core, pipelines/FSMs/fabrics unchecked. Commit precise status after diff/status review. This is approved future work, not implemented status.

The clean suite is source-test preprocess-test transfer-type-test syntax-model-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test rtl-name-test rtl-lower-test ready-graph-test output-model-test sv-emit-test vertical-slice-test, after make clean, then make verify. Search rtl.c/rtl_lower.c/ready_graph.c/output.c/sv_emit.c/compile.c for strstr, marker, rewrite, generated.*lookup; search lowering for concrete source transfer enums and production for composer/lowering/emitter links: expected no matches. The former output file set PLAN.md/architecture/invariants is retired by David's knowledge-tree cutover. Update the existing semantic state/plan/next/invariant owners instead, marking only implemented/tested unlinked RTL slice work complete; leave production/full-core/features/deletion unchecked. Investigate tracked generated differences rather than discard them.
