---
status: green
revised_at: "2026-11-09T21:26:34+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is a pre-release C17 source-to-source compiler extending SystemVerilog with ready/valid transfers and inline pipeline, transfer, fabric, and FSM blocks. It emits synthesizable SystemVerilog. In pigen-fork the production frontend is the structured data-first compiler. It now lowers declarations, abstract inputs, static and buffered transfers with real primitives, and clocked processes; pipelines, FSMs and fabrics are not yet lowered to hardware.
