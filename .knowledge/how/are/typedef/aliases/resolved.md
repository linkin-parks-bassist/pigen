---
status: green
revised_at: "2026-11-16T10:42:30+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Packed data typedef aliases follow the model below. Native endpoint typedefs instead carry payload and complete canonical protocol contracts and cannot be used as packed data types (how/are/native/axi/type/aliases/represented.md).

A canonical alias stores typedef symbol ID and resolved target type ID separately. Width/projection/state/operation queries follow stored target without another symbol lookup. pigen_struct_member_for_type follows alias targets to the owning packed record schema, so member reads also work through typedef chains and constructed values. Capability checks are alias-transparent while exact alias identity can survive preserving operations. Source-order lexical eligibility/shadow barriers are syntax facts, distinct from canonical semantic targets.

Owned scalar and packed-record DATA typedefs emit valid SV from parsed structure at compilation-unit, module and package scope, preserving type names for retained helpers. Numerical widths and signedness are rewritten rather than copying Pigen spellings; emission and independent helper/native-pin evidence are owned by how/are/packed/struct/typedefs/represented/and/read.md.
