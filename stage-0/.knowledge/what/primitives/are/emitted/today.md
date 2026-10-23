---
status: green
revised_at: "2026-09-13T15:07:33+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

rtl/pigen_primitives.sv defines pigen_buf, pigen_fifo, pigen_skid and pigen_port, parameterized by payload type (FIFO also depth/pointer/count widths). Shared controls are clk/reset/clear/discard/force_valid/force_invalid/force_after_transfer plus input/output payload/valid/ready. The current backend instantiates primitives; planned IR will represent those connections structurally.
