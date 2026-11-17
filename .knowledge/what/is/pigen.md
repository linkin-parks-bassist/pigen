---
status: green
revised_at: "2026-11-17T17:54:46+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is a pre-release C17 source-to-source compiler extending SystemVerilog with ready/valid transfers and inline pipeline, transfer, fabric, and FSM blocks. It emits synthesizable SystemVerilog. In pigen-fork the production frontend is the structured data-first compiler: it lowers declarations, abstract inputs, static and buffered transfers, clocked processes, pipelines, FSMs, source-blind inline three-port fabric networks. Ordinary SystemVerilog module items are retained with their widths, signedness, events and cycles where recognized; parameterized Pigen children use structured specialization, with remaining contexts owned by how/to/connect/child/ports.md. Fabric hookup and checked topology are owned by how/to/write/prototype/fabrics.md.

Proof:

```bash
test -x ./pigen || make -s pigen
./pigen examples/buf_pipeline.pigen -o /tmp/pigen-definition-check.sv
test -s /tmp/pigen-definition-check.sv
```
