---
status: green
revised_at: "2026-11-16T14:05:40+11:00"
---

Use a provisional `extern module` header to declare the native contract of separately compiled SystemVerilog IP:

```systemverilog
extern module external_axis_register #(parameter uint[32] DATA_W=32)(
 input wire clk,input wire reset,
 input uint[DATA_W] axi4_stream s_axis,
 output uint[DATA_W] axi4_stream m_axis);
```

Instantiate it normally with `.s_axis(bus)` and `.m_axis(other_bus)` and optional parameter overrides. The actual SV implementation supplies `s_axis_tdata/tvalid/tready` and matching output pins. Memory-mapped contracts expand all AW/W/B/AR/R fields and controls, including reverse B/R directions and configured sidebands. Endpoint names choose the standard pin prefix; clock/reset inputs retain their declared names and explicit native timing bindings.

The header has no body or endmodule and emits no module definition. The separate SV file is compiled alongside generated SV. `examples/native_axi_external.pigen` and `examples/native_axi_external_ip.sv` form a complete example; their wrapper passes fatal Verilator lint (/tmp/pigen-native-external-example.sv).

src/syntax.c parses the header as a structured module with is_external. src/resolve.c carries that flag onto semantic modules and uses the normal child contract, role, timing and ownership checks. Generated instance lowering handles external instances in root/generated/specialized owners, flattens their native channels, and retains the actual external definition name rather than emitting a specialization body. Effective constant parameters are emitted positionally in declaration order, excluding locals. src/output.c suppresses external definitions and duplicate retained instances. The syntax and ergonomics remain revisable.

The contract explicitly asserts the foreign interface. Pigen does not infer an aggregate from arbitrary scalar pins or verify an unseen implementation's protocol behavior. The actual SV compilation/lint checks physical port compatibility. A differently named interface needs an appropriately named endpoint or an SV wrapper; arbitrary per-pin renaming is not implemented. No implicit width/protocol/sideband adaptation occurs.

tests/native_protocol_test.py:external_axi_tests supplies independent ordinary-SV per-channel queues. Eighteen profiles cover all three families, 32-bit DATA defaults and 64-bit overrides, address-width 32-to-40 and ID-width 2-to-4 overrides, low-active reset, direct/internal routed paths and generated instances. Scoreboards check stalls, queued reset, exact capacity, payload and sidebands. Twelve width/role/clock/sideband/address negatives reject and preserve output. Evidence: /tmp/pigen-extern-parameters.log; /tmp/pigen-extern-parameters-1pk44mr_.

tests/native_array_test.py:external_collection_tests adds eighteen four-lane profiles across all families: aggregate/positional bindings, internal routing, child projections, sibling fabrics and scalar bindings. Each lane/channel has independent stalls and scoreboard values. Fatal lint and simulation pass (/tmp/pigen-extern-collections.log).

The expanded sanitizer matrix found a constant-arena pointer lifetime defect in the full-AXI array fabric case (/tmp/pigen-extern-boundaries-asan.log). Its correction and successful failing-input replay are owned by how/are/analyzed/constants/contextualized.md. All 36 profiles and twelve negatives pass ASan/UBSan with leak detection and halt-on-error (/tmp/pigen-extern-boundaries-asan-fixed.log; /tmp/pigen-extern-boundaries-asan-fixed-c2m0hvax); the full repository gate passes (/tmp/pigen-extern-boundary-final-verify.log). These channel witnesses do not replace legal-transaction verification of each implementation.
