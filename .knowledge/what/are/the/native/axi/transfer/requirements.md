---
status: green
revised_at: "2026-11-17T12:35:05+11:00"
---

Pigen must support AXI4-Stream, AXI4-Lite and full AXI4 as native transfer types alongside existing transfers. Earlier AXI versions are outside this requirement. Declarations, transfers and child-module connections should feel natural within Pigen's implicit handshake model.

David prioritizes native AXI module inputs/outputs. Native AXI fabrics are quarantined, disconnected from compiler entry points and removed from the README pending his architectural review. AXI declarations are restricted to module/fabric input/output boundaries because complete protocol endpoints are heavy for internal datapaths. The design must still let parent logic drive and consume a child's AXI channels and unpack channel records into ordinary transfer types. Standalone internal AXI declarations are forbidden. Parent code uses existing child.port endpoint access without declaring a second internal bus. Preserve type aliases for boundary contracts and remove unsupported internal-storage role exceptions and test/example modes. what/is/the/proposed/native/axi/type/model.md owns the model.

General SystemVerilog expansion is separate work unless a concrete module-boundary or fabric use establishes a dependency. Do not make broader compatibility work an AXI completion gate.

An AXI endpoint carries a complete protocol contract: channel directions, payload fields, widths and applicable sidebands. Equal payload width does not make an arbitrary Pigen transfer compatible with an AXI port. Conversion must define missing protocol information and behavior.

AXI4-Lite and AXI4 have independent address, data and response channels, including return channels. Preserve independent handshakes and protocol obligations. AXI4-Stream carries enabled sidebands with each beat. Whole transactions have protocol sequencing beyond a single atomic transfer.

Preserve generated standard-pin and child-connection verification for every family. Any future reactivation of native fabrics requires explicit review of contracts, write address/data association, response ownership, contention, backpressure and address parameterization. The existing work is retained as inactive reference material.

Memory-mapped AXI4/Lite interconnects require address-map and transaction-routing semantics distinct from Stream routing. A separate fabric spelling remains an open design choice.

Syntax and ergonomics remain revisable. Requirement evidence belongs to what/is/the/native/axi/acceptance/status.md; channel transport alone does not establish complete legal transactions.

Source: David's native AXI request, module/fabric priority and discussion of boundary-only declarations. Protocol reference: Arm AMBA 4, https://www.arm.com/architecture/system-architectures/amba/amba-4.
