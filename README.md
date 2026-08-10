# Pigen

Pigen (short for **pi**peline **gen**erator) is an extension of SystemVerilog
for describing synchronous hardware the way people tend to think about it:
values move through stages, wait in queues, meet at joins, and travel between
named endpoints.

It is an ennicening of Verilog: a Pythonisation of the language without turning
it into HLS. Clocks, widths, registers, backpressure, and cycle behavior remain
explicit hardware, while the compiler handles the endless ready/valid
bookkeeping. It should be possible to write serious RTL and still have a good
time doing it.

Pigen compiles `.pigen` source to readable, synthesizable SystemVerilog. Most
SystemVerilog remains exactly as it is; Pigen adds a small set of constructs for
transport values, elastic pipelines, fabrics, and state machines.

## A small example

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

    always_ff @(posedge clk) begin
        decode <= incoming;
        queue <= decode;
        outgoing <= queue;
    end
endmodule
```

Each `<=` is an atomic transfer. It happens when its source values are valid
and its destination can accept them. If the output stalls, backpressure travels
through the queue and pipeline without hand-written valid bits, ready equations,
or update-priority logic.

Joins look like the arithmetic they perform:

```systemverilog
always_ff @(posedge clk) begin
    product <= sample * coefficient;
    result <= product[23:8] + bias;
end
```

`product` waits until both inputs exist. Neither input is consumed alone. The
result waits until its destination can accept it.

## The language

Pigen adds a few complementary ways to describe hardware.

### Transport values

Transport declarations say where a value can wait and how it participates in
ready/valid flow:

```systemverilog
wire [7:0] combinational;
buf signed [15:0] stage;
skid packet_t elastic_pair;
fifo [31:0][8] work_queue;
port [31:0] pulse;
```

Assignments compose into pipelines, joins, guarded transfers, and co-sliced
atomic operations. `valid`, `ready`, `accepts`, `peek`, `validate`,
`invalidate`, and `flush` expose the handshake and storage state when control
logic needs it.

### Pipelines

A `pipeline` describes a typed sequence of elastic transforms:

```systemverilog
pipeline scale_and_bias #(
    parameter integer W = 16
) begin
    option skid_step = 0;

    stage multiply {sample, scale, bias} yields {product, bias} begin
        logic signed [W-1:0] sample;
        logic signed [W-1:0] scale;
        logic signed [W-1:0] bias;
        logic signed [2*W-1:0] product = sample * scale;
        skid;
    endstage

    stage add {product, bias} yields {logic signed [W-1:0] result} begin
        result = product[W-1:0] + bias;
    endstage
endpipeline
```

Stages are one-entry elastic transforms. Tuple types can be declared where a
value first appears and inherited by later stages. Pigen checks adjacent tuple
arity and widths, packs the stage interfaces, and inserts optional skid buffers.

### Fabrics

A `fabric` describes connections between named output and input endpoints:

```systemverilog
fabric sample_network #(
    parameter integer PAYLOAD_W = 16
) begin
    calibration.tx.coefficient > filter.coefficient;
    adc_left.tx.samples -> mixer.samples.left;
    adc_right.tx.samples --> mixer.samples.right;
endfabric
```

`>` is a direct exclusive connection. Dashed arrows use the routed fabric.
Pigen builds the endpoint queues and blind source-routed network, computes and
checks the routes, and emits an ordinary ready/valid SystemVerilog module.

### State machines

An `fsm` keeps state-oriented control next to the transport operations it
controls:

```systemverilog
fsm sender @(posedge clk) reset (reset) initial idle begin
    state idle: begin
        if (valid(in_packet))
            goto send;
    end

    state send: begin
        out_packet <= in_packet;
        if (accepts(out_packet, in_packet))
            goto idle;
    end
end
```

## What the compiler guarantees

Pigen rejects ambiguous ownership and partial transfers instead of producing
fragile handshake logic. Buffered values have one consumer; joins consume all
their inputs together; writes must be mutually exclusive; transport values stay
within one synchronous domain; and generated storage preserves ordering and
payload stability under backpressure.

The output is ordinary SystemVerilog with explicit nets and small storage
primitives from [`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv). It can be
read, linted, simulated, and synthesized with normal HDL tools.

## Build and try it

Pigen requires a C17 compiler. The regression suite also uses Icarus Verilog
and Verilator.

```sh
make
./pigen design.pigen
./pigen design.pigen -o generated.sv
make verify
```

Good starting points are:

- [`examples/native_blocks.pigen`](examples/native_blocks.pigen) for pipelines
  and fabrics in one source
- [`examples/fixed_point_mac.pigen`](examples/fixed_point_mac.pigen) for an
  elastic fixed-point datapath
- [`examples/intended_biquad.pigen`](examples/intended_biquad.pigen) for a
  larger recursive filter
- [`USER_GUIDE.md`](USER_GUIDE.md) for a practical walkthrough
- [`SPEC.md`](SPEC.md) for the complete language contract

## Current shape

The transport language covers elastic buffers and FIFOs, atomic joins and
co-sliced transfers, guarded flow, synchronous memories, explicit validity
control, and FSMs. Pipelines support packed typed tuples, parameters, inherited
types, combinational stage bodies, and configurable skid placement. Fabrics
support direct and routed connections, endpoint buffering, rotating routes,
round-robin arbitration, and recognized-source constants.

The fabric implementation currently uses one payload width per fabric, fixed
two-entry endpoint and router buffers, and a deterministic balanced topology.
Arrow tiers and the `objective` option are accepted but do not yet influence
topology optimization.
