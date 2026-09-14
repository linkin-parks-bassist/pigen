---
status: "unverified"
created_at: "2026-09-14T20:35:24+10:00"
scope: "local"
source: "SPEC.md Transfers; USER_GUIDE.md Ports and inferred memory; tests/port_bram.pigen location; retirement audit Codex /root 2026-09-14"
---

Use a local port for a synchronous memory read offered for one cycle. `if (read_enable) read_data <= mem[read_address];` emits an unconditional clocked payload assignment to retain RAM inference, while read_enable qualifies next-cycle valid. An unaccepted port pulse is lost, so use buf/skid/fifo when backpressure must preserve delivery. A conventional `mem[write_address] <= write_data;` with an input signal is preserved as a memory assignment, enabled by source validity, and drives source ready when it can accept. tests/port_bram.pigen is the executable read/write fixture; `make bram-waveform` produces its VCD. Target annotations are data-first; quarantined production spellings are described by `how/to/write/runnable/prototype/declarations.md`.
