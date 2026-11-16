---
status: green
revised_at: "2026-11-16T20:41:02+11:00"
---

Pigen must support AXI4-Stream, AXI4-Lite and full AXI4 as native transfer types alongside existing transfers. Earlier AXI versions are outside this requirement. AXI integration is a primary product requirement: declarations, transfers and child-module connections should feel as natural as existing Pigen types.

David's delivery priority is AXI as module inputs/outputs and in fabrics. General SystemVerilog function, procedural or generate-language expansion is separate compiler work unless a concrete module-boundary or fabric use establishes it as a dependency. Do not make every broader compatibility wishlist item a native AXI completion gate.

A child AXI endpoint carries a protocol contract, including channel directions, payload fields, widths and applicable sidebands. Connection checking must account for that entire contract. Equal payload width alone cannot make an arbitrary Pigen transfer compatible with an AXI port. Conversion must define the missing protocol information and behavior rather than silently invent or discard it.

The language must represent AXI4-Lite and AXI4's independent address, data and response channels, including the return direction. It must preserve independent handshakes and protocol obligations rather than treating a complete bus transaction as one existing atomic transfer. AXI4-Stream must carry its selected sidebands with the data beat.

Support must work throughout native composition, including fabrics. Fabric connections must carry complete protocol contracts, preserve AXI4 write address/data association and return responses to the correct initiator under contention and backpressure. Verification must exercise generated standard pins, child connections and fabric routes for all three families. Address windows must accept compile-time parameterization and per-instance module overrides.

Memory-mapped AXI4/Lite interconnects need address-map and transaction-routing semantics distinct from Stream routing. David has suggested that their fabric surface may deserve a separate form; a distinct spelling is an open design choice, not settled grammar.

The user has deliberately left syntax and ergonomics open for substantial revision. The candidate model is owned by what/is/the/proposed/native/axi/type/model.md. Requirement-by-requirement evidence belongs to what/is/the/native/axi/acceptance/status.md. Channel transport alone does not establish complete legal transactions or fabric correctness.

Source: David's original request and clarification prioritizing module inputs/outputs and fabrics. Protocol reference: Arm AMBA 4, https://www.arm.com/architecture/system-architectures/amba/amba-4.
