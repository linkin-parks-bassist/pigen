---
status: green
revised_at: "2026-10-31T11:03:49+11:00"
---

Use a local port for a synchronous memory read offered for one cycle: `if (read_enable) read_data <= mem[read_address];` is the intended shape, with read_enable qualifying the port's one-cycle valid. A port is a directly written payload register with ready=1; an unaccepted port pulse is lost, so use buf/skid/fifo when backpressure must preserve delivery. A conventional `mem[write_address] <= write_data;` with an input signal is the intended write shape.

Current production boundary: inferred memories (reg arrays) are not a specified Pigen construct, and the production structured path does not yet lower them - a data-first design containing a reg array fails at "RTL transfer lowering". The prototype-dialect fixture tests/port_bram.pigen (transfer-first `input port [7:0] data_in`) is diagnosed by the production frontend ("data type must precede the transfer type"), so `make bram-waveform` is red as per-subsystem migration debt (what/is/broken.md) until the port/memory lowering lands. Target annotations are data-first; quarantined production spellings are described by `how/to/write/runnable/prototype/declarations.md`.
