---
status: green
revised_at: "2026-11-16T04:33:17+11:00"
---

Declare a standalone fabric with a typed module-style port list and a declaration/connection body:

```systemverilog
fabric router #(parameter DATA_BITS=32) (
    input wire clk, input wire reset,
    input uint[DATA_BITS] axi4_lite[addr=32] s0,
    input uint[DATA_BITS] axi4_lite[addr=32] s1,
    output uint[DATA_BITS] axi4_lite[addr=32] m
) begin
    typedef uint[DATA_BITS] axi4_lite[addr=32] local_bus_t;
    local_bus_t fifo[2] middle;
    s0 -> middle;
    s1 -> middle;
    middle -> m;
endfabric
```

Instantiate router like a module, including named parameter overrides and aggregate native bindings. The declaration emits an ordinary SV module and uses structured semantic endpoints, contracts, clock/reset, ownership and channel storage. Its body accepts data-first local storage and typedefs plus scalar or matching-collection connection statements (how/are/native/axi/endpoint/arrays/represented.md); there is no PAYLOAD_W or implicit protocol conversion. Direct > and routed -> have the same laws as inline native fabrics. Memory-mapped `s -> m at [BASE:LIMIT];` links support inclusive address-window switching and DECERR outside the mapped range (how/does/native/memory/mapped/address/routing/work.md). All three AXI families are supported; memory-mapped responses reverse direction and full AXI fan-in needs target ID tag space. Topology and transaction limits are in how/are/native/axi/fabrics/connected/and/arbitrated.md.

src/blocks.c recognizes a top-level fabric port list after optional parameters and leaves it to the structured frontend. src/syntax.c parses it as a module owning one fabric node, ending its header at begin and body at endfabric. parse_fabric_block reuses parse_data_first_declaration/commit_declaration and parse_typedef for standalone body declarations, publishing them in the owning module scope while edges remain children of the fabric node. fabric_connection_at recognizes identifier/member/index endpoint prefixes followed by an arrow before declaration speculation; `s[0] -> m[0]` must remain a connection rather than a packed-count type. Alias contracts can reference module parameters; generated SV contains the resulting storage rather than native typedef text. Native child discovery recognizes typed fabric definitions as well as module definitions. Parameter overrides use typed semantic specialization, including module-root calls (how/are/generated/child/parameters/specialized.md). src/specialize.c recognizes the declaration boundaries and defers native calls. Legacy parameter-only PAYLOAD_W fabrics retain their separate router/SVG driver path (how/to/write/prototype/fabrics.md).

Evidence: standalone cases in tests/native_axis_fabric_test.py and tests/native_axi_fabric_test.py exercise standard-pin arbitration, sidebands, stalls and queued reset. tests/native_axi_core_fabric_test.py instantiates a parameterized typed fabric with WIDTH overridden from 64 to 32 before real native cores, using independent transaction scoreboards. Internal profiles cover local native aliases, fifo storage and direct/routed transit for all three families over three seeds. Stream standalone merge profiles check packet/beat ownership through internal storage. examples/native_axi_named_types.pigen demonstrates a parameterized full-AXI standalone fabric with local alias chains and address/ID overrides. Syntax remains provisional under the native AXI requirements.
