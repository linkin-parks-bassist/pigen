---
status: green
revised_at: "2026-09-14T20:48:47+10:00"
checked_at: '2026-09-14T17:13:16+10:00'
---

include/pigen/ids.h creates distinct single-field struct types with uint32_t index, preventing accidental cross-owner interchange at compile time. UINT32_MAX is PIGEN_INVALID_ID. Arena index implements stable identity; source spelling does not. ids.h carries eight distinct RTL identities (type, expression, object, instance, equation, update, process, module) used by the pigen_rtl_model arena.
