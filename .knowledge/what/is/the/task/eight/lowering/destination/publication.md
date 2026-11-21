---
status: green
revised_at: "2026-11-21T23:20:25+11:00"
---

pigen_lower_rtl_transfers in src/rtl_lower.c lowers resolved destination forms using declaration endpoints and the shared transfer fire expression. It accepts supported static lvalues, projections, selected/indexed paths, concatenations and whole buffered destinations. Unsupported identities fail through the normal invalid/-1 path, without a rollback guarantee.

Concatenation writes resize the whole RHS before splitting. Each member slice is reconstructed with an explicit conversion into the member's declared type, so mixed-width Pigen integer members lower correctly even though the aggregate is an ordinary packed vector.

For procedural-variable destinations, the RTL update's destination is the payload object, value is the converted RHS data, and enable is the fire expression. Selected/indexed writes also retain their destination path. Combinational-net destinations use a fire-selected RHS/zero equation. Buffered destinations publish storage-input payload/valid drives that the storage owner combines. Source-ready equations describe consumption; their control values are not substituted for payload data.

Module composition retains owner ranges and one RTL process per semantic process, with ordinary source ordering for static writes. Buffered ownership is validated by resolution (what/is/the/task/eight/lowering/owner/level/buffered/exclusive/producer/rejection.md).

Structural coverage includes t8-fire-identity, t8-join, t8-projection, t8-static and t8-process-order in tests/rtl_lower_test.c. Shared-expression tests alone cannot prove data or scheduling correctness; the current guard witness's limitation is owned by what/is/elastic/rtl/task/eight.md. Independent RTL simulation through make verify supplies production behavior evidence. The API map is what/is/the/task/eight/lowering/owner/api/map.md.
