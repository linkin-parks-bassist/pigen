---
status: green
revised_at: "2026-11-10T13:05:36+11:00"
---

`typedef struct packed { <type> <name>; ... } name;` is parsed into a packed syntax type carrying an ordered member list. Resolution flattens it to an unsigned packed vector whose width is the sum of member widths (first member most significant) and creates the normal typedef alias.

Member metadata is stored per typedef symbol: member name span, resolved data type, and least-significant-bit offset. A member read `base.member` resolves the base type's alias symbol, finds the member, and becomes a range select `base[offset+width-1:offset]` typed as the member type. A member write on a static struct lowers as an update whose destination path is that same range select; a write to a member of a buffered struct is rejected because buffered destinations must be written whole. `struct_member_write_tb` pins field writes and readback.

Current diagnostics: unknown member access requires a struct value; unknown member names fail; packed struct members require a name and semicolon; unterminated packed structs fail; projected buffered destinations fail with `buffered signal destination must be written as a complete value`.
