---
status: green
revised_at: "2026-11-17T10:33:28+11:00"
---

Declare a standalone fabric with typed module-style ports and a connection body:

```systemverilog
fabric router #(parameter DATA_BITS=32) (
    input wire clk, input wire reset,
    input uint[DATA_BITS] axi4_lite[addr=32] s0,
    input uint[DATA_BITS] axi4_lite[addr=32] s1,
    output uint[DATA_BITS] axi4_lite[addr=32] m
) begin
    s0 -> m;
    s1 -> m;
endfabric
```

Instantiate router as a child with parameter overrides and whole-port native bindings. It emits an ordinary SV module using structured endpoints, contracts, clock/reset, ownership and channel storage. AXI declarations belong only on input/output ports; local AXI storage declarations are rejected, including typedef-based declarations. Type definitions remain legal. Ordinary data storage and compatible scalar/collection connections use shared parsing.

Direct > and routed -> follow inline native fabric laws. Memory-mapped s -> m at [BASE:LIMIT] supports inclusive address-window switching and DECERR outside mapped ranges (how/does/native/memory/mapped/address/routing/work.md). All three families preserve their channel directions. Full AXI fan-in requires target ID tag space; how/are/native/axi/fabrics/connected/and/arbitrated.md owns topology and transaction limits.

src/blocks.c leaves typed fabric headers to the structured frontend. src/syntax.c parses a module owning a fabric node, with begin/endfabric boundaries, and shares declaration/typedef parsing. fabric_connection_at recognizes endpoint paths followed by arrows before declaration speculation. Native child discovery recognizes module and typed fabric definitions. Parameter overrides use semantic specialization (how/are/generated/child/parameters/specialized.md). Legacy parameter-only PAYLOAD_W fabrics retain their router/SVG path (how/to/write/prototype/fabrics.md).

Boundary profiles in tests/native_axis_fabric_test.py and tests/native_axi_fabric_test.py verify standard-pin arbitration, sidebands, stalls and queued reset. tests/native_axi_core_fabric_test.py instantiates parameterized typed fabrics with WIDTH overridden from 64 to 32 before real cores. examples/native_axi_named_types.pigen demonstrates parameterized full AXI and boundary child bindings. tests/native_boundary_policy_test.py rejects local AXI storage in typed standalone bodies. Syntax remains revisable.
