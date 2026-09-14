---
status: "unverified"
created_at: "2026-09-13T15:05:52+10:00"
scope: "local"
source: "David's current-only knowledge instruction 2026-09-14; audited current contracts and inspected owner interfaces; Codex /root"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:48:47+10:00"
---

Verification and durable boundary. After the slice is implemented, run the clean structured C/backend/vertical-slice suite, make verify and side-channel/source-enum/quarantine checks. Investigate tracked generated changes; expected production effect is none. Update the current state/plan/next and invariant owners only for implemented and tested unlinked RTL work. Production integration, full core, pipelines, FSMs and fabrics remain pending until their own gates pass. This is approved future work, not implemented status.

The clean suite is source-test preprocess-test transfer-type-test syntax-model-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test rtl-name-test rtl-lower-test ready-graph-test output-model-test sv-emit-test vertical-slice-test, after make clean, then make verify. Search rtl.c/rtl_lower.c/ready_graph.c/output.c/sv_emit.c/compile.c for strstr, marker, rewrite, generated.*lookup; search lowering for concrete source transfer enums and production for composer/lowering/emitter links: expected no matches. Review diffs and precise acceptance evidence before a coherent status commit. Do not recreate standalone documentation.
