---
status: "unverified"
created_at: "2026-09-13T15:03:18+10:00"
scope: "local"
source: "docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:20+10:00"
---

Every multi-arena append is transactional: restore all affected counts on failure and leave no reachable partial object. Syntax/semantic/graph/lowering errors use owning provenance; emitter failures are only malformed IR, allocation and output I/O. Lowering map counts and output buffers must also avoid publishing partial results.
