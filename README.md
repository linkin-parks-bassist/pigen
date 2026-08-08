# Pigen

Pigen is one C17 compiler for a synthesizable hardware language built on
SystemVerilog. A `.pigen` source can freely mix ordinary SystemVerilog modules
with Pigen's transport syntax and its native `pipeline` and `fabric` design
units. One invocation parses the complete source and emits one readable,
synthesizable SystemVerilog file.

There is no separate pipeline compiler, fabric generator, dispatch layer, or
secondary pipeline/fabric file format. Pipelines and fabrics are language
features of the same compiler and the same `.pigen` source format.

The complete language contract lives in [`SPEC.md`](SPEC.md). Pigen's emitted
RTL uses the deliberately simple primitives in
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv): `pigen_buf`,
`pigen_fifo`, and `pigen_skid`. You can inspect, lint, and synthesize the
result without needing a special runtime.

For a practical first-day walkthrough—pipelines, joins, hierarchy, ports,
BRAMs, FSMs, and workflow—start with the [user guide](USER_GUIDE.md).

## Quick start

Build and run Pigen with:

```sh
make
./pigen example.pigen              # writes example.sv beside the input
./pigen example.pigen -o out.sv    # explicit destination
```

A stage-oriented pipeline is written as `pipeline NAME ... endpipeline`; a
blind source-routed interconnect is written as `fabric NAME ... endfabric`.
Both lower into separately instantiable SystemVerilog modules in the same
output file. See [the combined language example](examples/native_blocks.pigen)
and the focused [pipeline](tests/pipeline_block.pigen) and
[fabric](tests/fabric_block.pigen) regression sources.

To run the compiler checks and all simulations:

```sh
make verify
```

## One language, three design units

| Design unit | Purpose | What the compiler emits |
| --- | --- | --- |
| `module ... endmodule` | SystemVerilog plus Pigen transport declarations and actions | The module with explicit ready/valid plumbing and primitive instances |
| `pipeline ... endpipeline` | A typed sequence of elastic transform stages | A public pipeline module and its private stage/skid modules |
| `fabric ... endfabric` | Direct and blind source-routed endpoint connections | A public fabric module and, when needed, its private router/queue modules |

All three may occur in any top-level order in one source. The words `pipeline`
and `fabric` retain their ordinary identifier meaning inside SystemVerilog
design units; only top-level blocks introduce the new constructs.

Pipeline stages use packed tuples and a conventional `clk`, `reset`, `enable`
ready/valid interface. Fabrics currently use one `PAYLOAD_W` for all links,
fixed two-entry endpoint/router queues, and a deterministic balanced routed
topology. Arrow dash counts are retained as tiers, and `objective` is accepted,
but v0 does not yet optimize topology from those hints. These are compiler
limitations, not alternate workflows or external generators.

## The model in one minute

Pigen adds transport declarations to familiar SV. `buf`, `fifo`, and `skid`
hold values elastically; a `port` is a directly written payload register with a
one-cycle valid pulse. `wire`, `reg`, and ordinary `logic` remain useful for
always-available combinational or state values.

Within a synchronous `always_ff` domain, a transport assignment is atomic:

- `destination <= source;` fires only when the destination can accept and all
  transport operands on the right-hand side are valid.
- `sum <= left + right;` is an atomic join: it never consumes just one input.
- `valid(x)`, `ready(x)`, and `accepts(destination, source)` expose the
  handshake when control logic needs to see it.
- `if (destination <= source)` is a compact test-and-transfer: it enters the
  branch and performs the transfer on the same accepted cycle.
- `invalidate(x);` drops one offered item and `flush(x);` clears buffered
  storage.

The result is intentionally boring RTL: explicit ready/valid nets, simple
storage instances, and ordinary sequential logic. What disappears is the
repetitive wiring and the chance to accidentally make a partial join or lose
backpressure.

## Make pipelines feel like plumbing

Declare the places data can wait, then connect them.  Each `<=` is an atomic
ready/valid transfer: it waits when the next stage is full and naturally
pushes back through the whole chain.  No hand-written `valid`, `ready`, or
enable plumbing required.

```systemverilog
module easy_pipeline
    (
        input logic clk,
        input logic reset,
        input buf [31:0] incoming,
        output buf [31:0] outgoing
    );

    buf  [31:0] decode;
    fifo [31:0][8] queue;

    always_ff @(posedge clk)
    begin
        decode <= incoming;
        queue <= decode;
        outgoing <= queue;
    end
endmodule
```

## A concrete comparison: fixed-point MAC

Here is an 8.8 fixed-point multiply-add with an elastic product stage. The
three inputs are independently backpressured; the multiply is an atomic join,
and the output is buffered. In Pigen, the datapath is simply the datapath:

```systemverilog
buf [31:0] product;

always_ff @(posedge clk)
begin
    product <= sample * coefficient;
    result <= product[23:8] + bias;
end
```

The equivalent hand-written ready/valid SV needs explicit valid state, ready
equations, routing, and update priority for the intermediate and output
stages:

```systemverilog
logic [31:0] product;
logic product_valid, product_in_ready, product_out_ready;
logic result_in_ready;

assign result_in_ready = !result_valid || result_ready;
assign product_out_ready = result_in_ready && bias_valid;
assign product_in_ready = !product_valid || product_out_ready;

assign sample_ready = product_in_ready && coefficient_valid;
assign coefficient_ready = product_in_ready && sample_valid;
assign bias_ready = result_in_ready && product_valid;

always_ff @(posedge clk)
begin
    if (reset)
        product_valid <= 1'b0;
    else if (sample_valid && coefficient_valid && product_in_ready)
    begin
        product_valid <= 1'b1;
        product <= sample * coefficient;
    end
    else if (product_valid && product_out_ready)
        product_valid <= 1'b0;
end

always_ff @(posedge clk)
begin
    if (reset)
        result_valid <= 1'b0;
    else if (product_valid && bias_valid && result_in_ready)
    begin
        result_valid <= 1'b1;
        result <= product[23:8] + bias;
    end
    else if (result_valid && result_ready)
        result_valid <= 1'b0;
end
```

Both versions are included: [Pigen MAC](examples/fixed_point_mac.pigen) and
[hand-written SV MAC](examples/fixed_point_mac_vanilla.sv). The Pigen version
is simulated with input/output stalls by `make mac-waveform`, which writes
[its VCD](examples/fixed_point_mac.vcd). The vanilla version is linted by the
same target.

The same notation handles joins: all buffered inputs are consumed together,
never half a packet at a time.

```systemverilog
always_ff @(posedge clk)
begin
    // Fires only when both operands are available and sum can accept.
    sum <= left + right;

    // Test-and-transfer in one line.
    if (result <= sum)
        completed <= 1'b1;
end
```

Pigen also protects the shape of the flow: buffered values have one consumer,
and writes to the same transport destination must be provably on disjoint
control paths. Those restrictions make the generated routes unambiguous and
keep ownership visible in the source.

Guards simply decide when a route is live; the source stays put until that
route can actually accept it.

```systemverilog
always_ff @(posedge clk)
begin
    if (launch)
        request_queue <= request;

    if (flush_requested)
        flush(request_queue);
end
```

FIFO declarations always spell payload before depth: `fifo[7:0][4] queue;`.
The earlier depth-before-payload spelling is rejected.

## Interfaces stay SystemVerilog-shaped

At a module boundary, a transport declaration expands to the payload plus
ordinary `name_valid` and `name_ready` ports.  The compiler's `__pigen_*`
names are private implementation details and never part of the public module
interface.

Run `make waveform` for a ready/valid pipeline proof of concept. It checks
ordered delivery through three `pigen_buf` instances and writes
`examples/buf_pipeline.vcd`, viewable with `gtkwave examples/buf_pipeline.vcd`.

`make compiler-waveform` elaborates and simulates
`examples/compiler_pipeline.pigen`.  It validates the generated atomic join
for `sum <= a_value + b_value` and writes `examples/compiler_pipeline.vcd`.

`make fifo-waveform` elaborates `fifo[7:0][4]`, verifies that its first pushed item
is valid even while downstream is stalled, then fills and drains it under
Verilator while writing `examples/fifo_pipeline.vcd`.

`make skid-waveform` does the corresponding two-entry `skid` demonstration and
writes `examples/skid_pipeline.vcd`.

`make port-waveform` drives a sparse `input buf` stream into a `port`, checks
the one-cycle pulse behavior, and writes `examples/port_pipeline.vcd`.

`port` is also the timing-friendly bridge for inferred synchronous RAMs.  A
guarded write into an internal port emits an unconditional payload register
write and a separately gated valid pulse, so this source remains a natural
BRAM read shape:

```systemverilog
if (read_enable)
    bram_port <= mem[read_address];
```

Conversely, an ordinary memory write can consume an input transport directly:
`mem[write_address] <= data_in;`.  Pigen gates that write on `data_in` valid
and drives its ready handshake.  `make bram-waveform` exercises both paths and
writes `tests/port_bram.vcd`.

`make guarded-waveform` shows a guarded transport assignment holding its input
under a disabled guard, then transferring ordered values when enabled.

`make output-waveform` drives an `input buf` port through a compiler-lowered
`output buf` port under backpressure, checks ordered delivery, and writes
`examples/output_pipeline.vcd`.

## Examples and waveforms

| Example | What it demonstrates | Artifact |
| --- | --- | --- |
| [buf pipeline](examples/buf_pipeline.pigen) | Three explicit elastic stages under backpressure | [VCD](examples/buf_pipeline.vcd) |
| [expression pipeline](examples/compiler_pipeline.pigen) | Compiler-lowered `sum <= a_value + b_value` join | [VCD](examples/compiler_pipeline.vcd) |
| [fixed-point MAC](examples/fixed_point_mac.pigen) | Atomic multiply-add join, elastic product stage, and output backpressure | [VCD](examples/fixed_point_mac.vcd) |
| [buffered join pipeline](examples/join_pipeline.pigen) | Atomic join of two independently pulsed `input buf` values | [VCD](examples/join_pipeline.vcd) |
| [FIFO pipeline](examples/fifo_pipeline.pigen) | `fifo[7:0][4]` fill, backpressure, and drain | [VCD](examples/fifo_pipeline.vcd) |
| [skid pipeline](examples/skid_pipeline.pigen) | Exact two-entry skid capacity | [VCD](examples/skid_pipeline.vcd) |
| [port pipeline](examples/port_pipeline.pigen) | Sparse input becomes one-cycle valid pulses | [VCD](examples/port_pipeline.vcd) |
| [BRAM port test](tests/port_bram.pigen) | Input-port memory write plus separate-address delayed port read | [VCD](tests/port_bram.vcd) |
| [guarded pipeline](examples/guarded_pipeline.pigen) | `if (enable)` gates an elastic transfer | [VCD](examples/guarded_pipeline.vcd) |
| [output port pipeline](examples/output_pipeline.pigen) | Buffered module-boundary transfer under backpressure | [VCD](examples/output_pipeline.vcd) |

View a VCD with
`gtkwave examples/fifo_pipeline.vcd`, substituting any listed waveform.
