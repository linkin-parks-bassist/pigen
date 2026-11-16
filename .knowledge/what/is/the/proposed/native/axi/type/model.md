---
status: green
revised_at: "2026-11-17T10:34:54+11:00"
---

AXI is a native boundary transfer type. AXI4-Stream, AXI4-Lite and full AXI4 declarations are allowed only on module/fabric input/output ports. Native typedefs and package contracts remain reusable, but standalone internal AXI declarations are forbidden. Parent logic accesses child.port channels directly and constructs or unpacks typed records into ordinary transfer types (how/are/internal/native/axi/child/bindings/resolved.md). Source syntax remains revisable.

An endpoint describes protocol family, role, DATA/address/ID widths and optional fields. AXI4-Stream has a forward beat channel; Lite/full AXI have independent AW/W/B/AR/R channels, with B/R reversed. Each channel has its own transfer event. The whole memory-mapped endpoint has no single valid/ready/fire value.

Whole-endpoint child bindings check complementary roles, complete contracts and translated clock/reset identities, then connect channels and sidebands in their declared directions. Named/positional bindings, collections, generated children and sibling views preserve endpoint identity. Equal payload widths alone do not authorize protocol, width or sideband adaptation.

Protocol identity and channel storage are separate semantic properties. Boundary channels support buf/fifo/skid. Contract layout belongs in semantic types; standard pin names and flattening belong in boundary RTL lowering. Source realization must hold VALID and the complete beat until acceptance without waiting for READY to offer VALID. Independent AW/W acceptance, response backpressure, reset, burst/last/ID and ordering need protocol laws and independent RTL witnesses. Channel-level atomic transfers alone do not enforce complete transaction sequencing.

The standard Stream FIFO, Lite register bank and full bridge use native boundary endpoints and named records, with independent standard-pin scoreboards. Full AXI acceptance identifies its finite burst/ID/ordering profile. how/to/write/native/axi/declarations.md owns syntax and how/are/native/axi/endpoints/mapped/to/external/systemverilog/ip.md owns provisional extern contracts.

Memory-mapped and Stream fabrics have distinct implementations while sharing fabric syntax. A separate memory-mapped spelling remains open. Typed standalone fabrics and whole-bus links use parent ports and child views; how/are/native/axi/fabrics/connected/and/arbitrated.md owns topology, transaction ownership and ID tagging. Named records/defaults are implemented in assignment contexts; how/to/construct/typed/records.md owns their typing. Richer projections, pattern contexts, adapters and transaction conveniences require concrete boundary examples before expanding scope.

Protocol references: Arm AMBA AXI specification, https://documentation-service.arm.com/static/67ab5a186dbc975ccea91734, and https://www.arm.com/architecture/system-architectures/amba/amba-4. Compiler evidence: include/pigen/transfer_type.h, src/transfer_type.c, cores/axis, cores/axil and cores/axi4.
