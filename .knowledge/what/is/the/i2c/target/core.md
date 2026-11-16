---
status: green
revised_at: "2026-11-14T17:29:45+11:00"
---

cores/i2c/target.pigen implements a seven-bit addressed I2C target with native buffered byte TX/RX and open-drain scl_low/sda_low outputs. Address matches select read or write. Writes publish bytes and stretch SCL before ACK while RX is blocked; reads stretch before data until TX is available. A controller NACK ends reading. START/repeated START resets bus decoding, STOP releases the target, and reset flushes storage and state. A queued following TX byte survives a partial read, NACK and STOP; already-reserved partial data is discarded.

active indicates a matching transaction, reading its direction, and completed pulses on a selected STOP or read NACK. No address bytes enter RX. ADDRESS accepts 8..119; general call/reserved addresses, ten-bit addressing, spike filtering and SMBus timeout are outside this profile. SCL/SDA use two-stage synchronizers. Every physical high/low and START/STOP interval must be at least eight system clocks. SETUP_CYCLES (minimum two) controls stretched read/ACK setup. Choose physical timing and I/O constraints for the bus speed; simulation is not electrical qualification. Protocol reference: [NXP UM10204](https://www.nxp.com/docs/en/user-guide/UM10204.pdf).

tests/cores/i2c_target_tb.sv independently drives asynchronous open-drain pins. Nine address/timing profiles combine addresses 8/82/119 with 83/107/173 ns half-periods and setup counts 2/4/9 against a 10 ns system clock. Each checks all 128 addresses, exhaustive bytes in both directions, ACK/NACK, RX/TX stretching, repeated START, one-through-seven-bit partial address/write/read aborts, queued-byte preservation, reset and recovery: 261 RX bytes and 528 comparisons per run. Stimulus avoids the simulator's active sampling edge to eliminate testbench races.

tests/cores/run.py retains configured source, emitted SV, warnings-fatal Verilator lint, independent Icarus logs, CSV/VCD and hashes under build/cores/i2c-target-addr*-half*. Invalid address/setup configurations are rejected. The gallery includes the exact source and an internal-state interactive trace.
