---
status: green
revised_at: "2026-11-17T11:51:38+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Packed DATA typedef aliases retain a canonical typedef symbol ID and resolved target type ID separately. Width, projection, state and operation queries follow the target without repeating symbol lookup. pigen_struct_member_for_type follows alias chains to the packed record schema. Capability checks are alias-transparent while preserving operations can retain exact alias identity. Source-order eligibility and lexical shadows remain syntax facts.

Owned scalar and packed-record DATA typedefs emit parsed SV at compilation-unit, module and package scope, retaining names for ordinary helpers and rewriting numerical widths/signedness. how/are/packed/struct/typedefs/represented/and/read.md owns emission and independent evidence.

Native endpoint typedefs instead carry DATA and a complete protocol contract; they are not packed DATA types (how/are/native/axi/type/aliases/represented.md).

A current ordinary-SV compatibility gap concerns a DATA typedef named axi4. The valid SV declaration typedef logic [7:0] axi4; is interpreted as a native protocol qualifier and rejects with "typedef requires a name". Verilator accepts the independent ordinary module in /tmp/pigen-ordinary-protocol-names-5v6s7ub6/source.sv, while ./pigen rejects source.pigen before the boundary-only retained-declaration check. Disambiguating a typedef name from a protocol qualifier belongs to general namespace/parser compatibility work; it does not block the checked AXI boundary or pipeline profiles.
