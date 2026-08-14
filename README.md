# Pigen

Pigen (short for **pi**peline **gen**erator) is a SystemVerilog extension that
builds ready/valid handshake into the language. Its core feature is a small set
of transport types—`buf`, `port`, `skid`, and `fifo`—whose assignments move
complete packets only when the source is valid and the destination is ready.

It is an ennicening of Verilog: a Pythonisation of the language without turning
it into HLS. Clocks, widths, registers, storage choices, backpressure, and cycle
behavior remain explicit hardware. Pigen removes the repetitive valid bits,
ready equations, occupancy bookkeeping, and fragile partial-transfer logic.

Pigen compiles `.pigen` source to readable, synthesizable SystemVerilog. It is
intended as an incremental extension layer: an existing SV codebase should
retain its behavior when passed through Pigen, and transports can then be
introduced one boundary or datapath at a time. Deliberate restrictions must be
documented and diagnosed; silent semantic drift is never acceptable.

## Handshake is part of assignment

```systemverilog
module packet_path
    (
        input  logic clk,
        input  logic reset,
        input  buf [31:0] incoming,
        output buf [31:0] outgoing
    );

    buf  [31:0] decoded;
    skid [31:0] timing_break;
    fifo [31:0][8] pending;

    always @(posedge clk) begin
        decoded     <= incoming;
        timing_break <= decoded;
        pending     <= timing_break;
        outgoing    <= pending;
    end
endmodule
```

Each `<=` is one atomic transfer. It proceeds exactly when every source packet
is valid and every destination can accept it. Accepted buffered sources are
consumed; stalled packets remain stable. Backpressure propagates automatically
until a `skid` or `fifo` deliberately breaks the combinational ready path.

The declaration chooses the storage and timing behavior:

| Transport | Use it for | Behavior |
| --- | --- | --- |
| `buf T x` | The normal elastic pipeline step | One packet of storage; holds valid and payload under backpressure. |
| `port T x` | A direct register-shaped boundary or synchronous-memory result | One-cycle valid pulse; no backpressure storage, so an unaccepted output pulse can be lost. |
| `skid T x` | Breaking a ready timing path with minimal capacity | Exact two-packet skid storage with registered backpressure. |
| `fifo T[N] x` | Deliberate queueing and burst absorption | Ordered depth-`N` storage; also breaks the ready path. |

The same kinds can be used on module ports. A module can therefore expose the
delivery contract it needs without manually expanding every packet into
payload, valid, and ready signals.

## Packets compose like SystemVerilog values

Ordinary expressions form atomic joins:

```systemverilog
result <= sample * coefficient + bias;
```

The transfer waits for `sample`, `coefficient`, and `bias`, then consumes all
three together. None can disappear alone.

Concatenation joins and splits packet bit streams using normal SystemVerilog
ordering:

```systemverilog
packet <= {header, body};
{opcode, payload} <= packet;
{upper, lower} <= {wide_packet[15:8], wide_packet[7:0]};
```

Only the aggregate widths need to match. Every destination in a grouped split
must be ready before the source is consumed. A slice such as
`byte <= wide_packet[7:0]` consumes the complete `wide_packet` token; several
slices that belong together must be written as one grouped transfer. Constants
are always-valid, non-consuming source values.

Normal SV control flow remains available around transfers. `valid`, `ready`,
`accepts`, `peek`, `validate`, `invalidate`, and `flush` expose handshake and
occupancy state when explicit control is useful.

## Storage choices stay visible

Pigen does not infer a mysterious pipeline architecture from an untimed
algorithm. The source still says where a packet may wait and where a ready path
must end:

```systemverilog
buf  packet_t parsed;       // ordinary one-entry elastic step
skid packet_t scheduled;    // intentional ready-path break
fifo packet_t[16] queued;   // intentional traffic elasticity
port packet_t observed;     // pulse/register-shaped result
```

Changing a `buf` to a `skid` or `fifo` is a local, reviewable architectural
choice rather than a rewrite of handshake plumbing.

## Fabrics make hookup bearable

Transport-aware fabric syntax connects module endpoints without the usual wall
of payload/valid/ready wiring:

```systemverilog
fabric sample_network begin
    calibration.coefficient > filter.coefficient;
    adc_left.samples -> mixer.left;
    adc_right.samples --> mixer.right;
endfabric
```

`>` is a direct exclusive connection. Dashed arrows use routed fabric links.
The hookup syntax is useful in both prototypes and finished Pigen designs: it
keeps explicit interconnect readable and makes inserting endpoint `buf`, `port`,
`skid`, and `fifo` behavior much less painful.

Automatic topology and route generation provide a minimal-fuss way to get a
design working before its real traffic is understood. A mature design may
replace those automatic choices with explicit routing while keeping the fabric
description. Pigen emits an SVG of the elaborated endpoints, routers, and links
for either workflow, so the actual interconnect remains visible and reviewable.

<img src="docs/fabric.svg" alt="A Pigen fabric with endpoints and routed links" width="900">

Use `--diagram PATH` to choose the SVG path or `--no-diagram` to suppress it.
With several fabrics, each gets its own diagram.

## Convenience syntax

`pipeline` declarations and `fsm` blocks are higher-level conveniences built on the same
transport semantics. Pipelines package a sequence of typed elastic transforms;
FSMs keep state-oriented control next to the transfers it controls. They can
save source code, but they are not required to obtain Pigen's central benefit:
ordinary modules using `buf`, `port`, `skid`, `fifo`, and atomic assignments
already have language-level handshake.

## What the compiler guarantees

Pigen rejects ambiguous packet ownership and partial transfers. Buffered values
have one consumer; joins consume all their inputs together; grouped splits wait
for every destination; writes must be mutually exclusive; and stored payloads
remain stable under backpressure.

The output is ordinary SystemVerilog with explicit nets and small storage
primitives from [`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv). It can be
read, linted, simulated, and synthesized with normal HDL tools.

Pigen is pre-release. Its extension syntax, generated names, and generated
interfaces may still change. The ordinary-SystemVerilog compatibility contract
does not: extension development must not silently change existing SV behavior.

## Build and try it

Build Pigen with `make`. The test suite also uses Icarus Verilog and Verilator.

```sh
make
./pigen design.pigen -o generated.sv
./pigen network.pigen -o network.sv --diagram network.svg
make verify
```

Good starting points are:

- [`examples/buf_pipeline.pigen`](examples/buf_pipeline.pigen) for the basic
  elastic transfer model
- [`examples/join_pipeline.pigen`](examples/join_pipeline.pigen) for an atomic
  multi-source join
- [`examples/skid_pipeline.pigen`](examples/skid_pipeline.pigen) and
  [`examples/fifo_pipeline.pigen`](examples/fifo_pipeline.pigen) for explicit
  timing and queueing choices
- [`examples/port_pipeline.pigen`](examples/port_pipeline.pigen) for pulse-like
  `port` behavior
- [`examples/biquad_bank.pigen`](examples/biquad_bank.pigen) for an eight-slot,
  five-coefficient stateful DSP bank, including its clean unconfigured-slot path
- [`USER_GUIDE.md`](USER_GUIDE.md) for a practical walkthrough
- [`SPEC.md`](SPEC.md) for the complete language and compatibility contract

## Current limits

Fabric payload typing and topology control are still evolving. The current
automatic fabric uses one payload width, fixed endpoint/router buffering, and a
deterministic balanced topology; arrow tiers and `objective` do not yet drive
topology optimization. Aggregate co-slice widths are checked by the generated
SystemVerilog at elaboration time; reporting those mismatches directly at the
original Pigen source location is still planned.
