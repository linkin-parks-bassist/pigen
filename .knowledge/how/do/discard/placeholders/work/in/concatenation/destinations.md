---
status: green
revised_at: "2026-11-10T13:14:02+11:00"
---

In a concatenation destination, an underscore item (`_`) is a discard placeholder: `{x, _} <= {expr, ignored};`.

Resolution requires the destination and RHS to be concatenations of equal child count. The placeholder child takes the matching RHS child's data type; other children resolve as normal lvalues. Lowering advances past the placeholder's width without creating a destination update, while the matching RHS source is still part of the transfer and its token is consumed. `concat_placeholder_tb` pins that the non-placeholder destination receives its field and the placeholder field is discarded.
