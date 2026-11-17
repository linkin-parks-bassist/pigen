---
status: green
revised_at: "2026-11-17T16:30:13+11:00"
---

Remaining repository work.

1. Remove legacy optional FSM block names; enforce anonymous fsm @(posedge clk) begin and update affected syntax/tests.
2. Resume general SV/compiler compatibility separately from AXI delivery. Integrate generated retained helper scopes, opaque/extern signatures and impure function/task scheduling (how/are/retained/native/axi/observations/represented.md; how/are/retained/subroutine/argument/effects/checked.md). Extend analyzed constants, expression sizing/entry points, fill literals, unpacked type-key patterns/concatenations/slices/typedef casts, collection binding contexts and continuous-assignment lvalues. Finish named-block forward/retained hierarchy, function/task contexts, retained/native namespace and driver audits, generated enum names, opaque-import exports and retained type/value-shadow grammar, including DATA typedef names colliding with protocol-family spellings (how/are/typedef/aliases/resolved.md). Extend ordinary packages to classes, DPI/extern/prototypes, variables and enums/unions, then typed subroutine evaluation. Continue effectful mixed-case qualification/scheduling, remaining qualified-if contexts, sequential case/loop lowering and nonblocking reconstruction. Governing owners: what/is/the/spec.md, what/is/broken.md, how/are/ordinary/package/dependencies/resolved.md and how/are/scoped/combinational/processes/represented.md.
3. Expand native core profiles and transaction composition beyond existing UART/SPI/I2C/I2S/Stream/Lite/SDRAM/AXI4 coverage; extend framing, widths and memory throughput. Preserve independent RTL evidence and browser acceptance. Views must expose RHS/control semantics, preserve transform ready/valid, AND combining valids, omit reset cases and separate write-enable indicators, retain assign dependencies, collapse raw SV, preserve compact layouts/uniform clocks/stable controls and pass visual checks. Board qualification needs SDRAM part/temperature/geometry/clock selection, forwarded-clock phase and FPGA I/O constraints.

After substantive work, harden nested/partial/spanning and multidimensional aliases, multiword constants/powers, standalone negative typing, unresolved bounds, writer/cross-clock audits, specialization provenance, broader compatibility and independent fire/ready probes against actual update enables (what/is/elastic/rtl/task/eight.md). Preserve interrupted work.

Reconcile fabric lowering with the source-blind inline contract: move the balanced three-port tree behind resolved child-port endpoints and remove the legacy fixed-width dialect/path interface. The governing direction is explicit in SPEC.md at d14aef2 and reaffirmed by David: connect exclusively actual top-level module ports; sender identity belongs in packet DATA when needed, and producers need no consumer knowledge. Preserve distinct endpoint readiness rather than hiding shared-queue dependencies behind handle-like aliases (how/to/write/prototype/fabrics.md).

Open decisions: FSM startup without asserted reset; computed automatic-input validity; pipeline yield surface; fabric manifest/SVG retention; struct-like child instantiation; whether native AXI interconnects should be reactivated, redesigned or retired after David reviews the quarantined work. No AXI fabric extension is planned before that review.

Deferred to Pigen 1.0: ready-cycle validation and route negative witnesses. Stages diagnose and stop without rollback.
