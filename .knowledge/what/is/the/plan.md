---
status: green
revised_at: "2026-11-17T10:19:14+11:00"
---

Remaining repository work, separate from native AXI module-interface/fabric delivery.

First repair production pipeline field versioning. Give each boundary distinct field identities so x <= x + 1 reads incoming x and writes outgoing x; preserve same-stage snapshot reads, carry-forward, guards, reset and packet-wide handshakes. Make authored stage boundaries control packet storage and atomic acceptance, rather than merely grouping ordinary field transfers. Add end-to-end repeated-name and multi-field simulations with downstream stalls, including a witness that distinguishes merged from split stages. The current failure and contract are owned by how/do/pipeline/stage/reads/work.md.

1. Remove legacy optional FSM block names; enforce anonymous fsm @(posedge clk) begin and update affected syntax/tests.
2. Resume general SV/compiler compatibility separately from AXI delivery. Integrate generated retained helper scopes, opaque/extern signatures and impure function/task scheduling (how/are/retained/native/axi/observations/represented.md; how/are/retained/subroutine/argument/effects/checked.md). Extend analyzed constants, expression sizing/entry points, fill literals, unpacked type-key patterns/concatenations/slices/typedef casts, collection binding contexts and continuous-assignment lvalues. Finish named-block forward/retained hierarchy, function/task contexts, retained/native namespace and driver audits, generated enum names, opaque-import exports and retained type/value-shadow grammar. Extend ordinary packages to classes, DPI/extern/prototypes, variables and enums/unions, then typed subroutine evaluation. Continue effectful mixed-case qualification/scheduling, remaining qualified-if contexts, sequential case/loop lowering and nonblocking reconstruction. Governing owners: what/is/the/spec.md, what/is/broken.md, how/are/ordinary/package/dependencies/resolved.md and how/are/scoped/combinational/processes/represented.md.
3. Expand native core profiles and transaction composition beyond existing UART/SPI/I2C/I2S/Stream/Lite/SDRAM/AXI4 coverage; extend framing, widths and memory throughput. Preserve independent RTL evidence and browser acceptance. Views must expose RHS/control semantics, preserve transform ready/valid, AND combining valids, omit reset cases and separate write-enable indicators, retain assign dependencies, collapse raw SV, preserve compact layouts/uniform clocks/stable controls and pass visual checks. Board qualification needs SDRAM part/temperature/geometry/clock selection, forwarded-clock phase and FPGA I/O constraints.

After substantive work, harden nested/partial/spanning and multidimensional aliases, multiword constants/powers, standalone negative typing, unresolved bounds, writer/cross-clock audits, specialization provenance, broader compatibility and independent fire/ready probes against actual update enables (what/is/elastic/rtl/task/eight.md). Preserve interrupted work.

Open decisions: FSM startup without asserted reset; computed automatic-input validity; pipeline yield surface; fabric manifest/SVG retention; struct-like child instantiation; distinct memory-mapped fabric surface.

Deferred to Pigen 1.0: ready-cycle validation and route negative witnesses. Stages diagnose and stop without rollback.
