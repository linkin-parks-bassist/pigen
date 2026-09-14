---
status: "unverified"
created_at: "2026-09-13T15:04:48+10:00"
scope: "local"
source: "include/pigen/ids.h; commit 30ac091; elastic RTL plan Task 1"
ingested_by: "Codex /root"
checked_at: "2026-09-14T17:13:16+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T17:13:16+10:00"
updated_by: "opencode"
---

include/pigen/ids.h creates distinct single-field struct types with uint32_t index, preventing accidental cross-owner interchange at compile time. UINT32_MAX is PIGEN_INVALID_ID. Arena index implements stable identity; source spelling does not. The generic pigen_rtl_id placeholder was removed in commit 30ac091; ids.h now carries eight distinct RTL identities (type, expression, object, instance, equation, update, process, module) used by the pigen_rtl_model arena.
