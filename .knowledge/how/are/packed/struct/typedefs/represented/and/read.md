---
status: green
revised_at: "2026-11-10T12:29:18+11:00"
---

`typedef struct packed { <type> <name>; ... } name;` is parsed into a packed syntax type carrying an ordered member list. Resolution flattens it to an unsigned packed vector whose width is the sum of member widths (first member most significant) and creates the normal typedef alias.

Member metadata is stored per typedef symbol: member name span, resolved data type, and least-significant-bit offset. A member read `base.member` resolves the base type's alias symbol, finds the member, and becomes a range select `base[offset+width-1:offset]` typed as the member type. Member writes are not implemented yet.

Current diagnostics: unknown member access requires a struct value; unknown member names fail; packed struct members require a name and semicolon; unterminated packed structs fail.
