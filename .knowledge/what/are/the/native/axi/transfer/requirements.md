---
status: green
revised_at: "2026-11-17T10:21:43+11:00"
---

Pigen must support AXI4-Stream, AXI4-Lite and full AXI4 as native transfer types alongside existing transfers. Earlier AXI versions are outside this requirement. Declarations, transfers and child-module connections should feel natural within Pigen's implicit handshake model.

David prioritizes module inputs/outputs and fabrics. He is considering restricting AXI declarations to boundaries because complete protocol endpoints are heavy for internal datapaths. The design must still let parent logic drive and consume a child's AXI channels and unpack channel records into ordinary transfer types. Whether to ban standalone internal AXI declarations remains open; no such restriction is implemented. Existing child.port endpoint access offers a candidate surface without declaring a second internal bus. what/is/the/proposed/native/axi/type/model.md owns the model.

General SystemVerilog expansion is separate work unless a concrete module-boundary or fabric use establishes a dependency. Do not make broader compatibility work an AXI completion gate.

An AXI endpoint carries a complete protocol contract: channel directions, payload fields, widths and applicable sidebands. Equal payload width does not make an arbitrary Pigen transfer compatible with an AXI port. Conversion must define missing protocol information and behavior.

AXI4-Lite and AXI4 have independent address, data and response channels, including return channels. Preserve independent handshakes and protocol obligations. AXI4-Stream carries enabled sidebands with each beat. Whole transactions have protocol sequencing beyond a single atomic transfer.

Fabric composition must preserve contracts, write address/data association and response ownership under contention and backpressure. Verify generated standard pins, child connections and fabric routes for every family. Address windows accept compile-time parameters and per-instance overrides.

Memory-mapped AXI4/Lite interconnects require address-map and transaction-routing semantics distinct from Stream routing. A separate fabric spelling remains an open design choice.

Syntax and ergonomics remain revisable. Requirement evidence belongs to what/is/the/native/axi/acceptance/status.md; channel transport alone does not establish complete legal transactions.

Source: David's native AXI request, module/fabric priority and discussion of boundary-only declarations. Protocol reference: Arm AMBA 4, https://www.arm.com/architecture/system-architectures/amba/amba-4.
