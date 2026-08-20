# Pigen

Pigen is a SystemVerilog extension for describing synchronous, flow-controlled
hardware. It makes ready/valid transfer a language concept: values move when
the source has a packet and the destination can accept it, while the compiler
generates the valid bits, ready paths, storage control, and backpressure logic.

The aim is an ennicening of Verilog. Widths, clocks, registers, storage, timing
boundaries, and cycle behaviour remain explicit hardware choices, but the
repetitive and error-prone mechanics of moving packets do not dominate the
source.

Pigen compiles `.pigen` files to readable, synthesizable SystemVerilog. Ordinary
SystemVerilog remains part of the language and has a strict compatibility
contract: accepted SV must retain its observable behaviour when processed by
Pigen.

Pigen is pre-release. The compiler already implements the core transfer model,
elastic pipelines, queues, FSMs, and routed fabrics. Its extension syntax and
compiler architecture are being revised as the language takes its intended
shape.

## The basic idea

This is accepted by the current compiler:

```systemverilog
module packet_path
    (
        input  logic        clk,
        input  logic        reset,
        input  buf [31:0]   incoming,
        output buf [31:0]   outgoing
    );

    buf  [31:0] decoded;
    skid [31:0] timing_break;
    fifo [31:0][8] pending;

    always_ff @(posedge clk) begin
        decoded      <= incoming;
        timing_break <= decoded;
        pending      <= timing_break;
        outgoing     <= pending;
    end
endmodule
```

Each `<=` involving transports is an atomic transfer. It happens on the clock
edge when its sources are valid and its destinations are ready. A stalled
packet stays in place, and backpressure propagates to the producer. `skid` and
`fifo` storage deliberately end the combinational ready path.

Expressions form atomic joins:

```systemverilog
result <= sample * coefficient + bias;
```

The transfer waits for all three packets and consumes them together.
Concatenations provide atomic joins and splits using ordinary SystemVerilog bit
ordering:

```systemverilog
packet <= {header, body};
{opcode, payload} <= packet;
{upper, lower} <= {wide_packet[15:8], wide_packet[7:0]};
```

Every destination in a split must be ready before the source is consumed. A
projection such as `wide_packet[7:0]` still consumes the complete packet token;
grouping the related projections states that they belong to one transfer.

Normal SystemVerilog control flow surrounds transfers. Pigen understands the
guards well enough to prove common mutually exclusive routes, and provides
`valid`, `ready`, `accepts`, `peek`, `validate`, `invalidate`, and `flush` when
control logic needs to inspect or alter transport state explicitly.

## Data and movement

The language is moving toward a clean separation between two concerns:

- a data type describes the value and its width;
- a transport kind describes how that value waits and moves.

The intended declaration order is data type, optional transport kind, then
name:

```systemverilog
int[16] buf  x;
int[16] buf  arr[8];
bit     port finished;
byte         tag;
```

`int[n]`, `uint[n]`, `bit`, and `byte` are the initial planned data types.
`[n]` is width notation and lowers to the usual SystemVerilog `[n-1:0]` packed
range. Array dimensions remain after the name, as in the `arr` declaration.
Ordinary SystemVerilog declarations such as `logic [21:0] h[0:12];` retain their
ordinary meaning.

The current compiler still accepts the transport-first syntax shown in the
opening example. Because Pigen has no legacy syntax obligation, the new form
will replace it outright when its type rules and frontend are complete; there
will not be two Pigen declaration dialects.

The current transport kinds express these storage and timing choices:

| Kind | Role |
| --- | --- |
| `buf` | A one-packet elastic stage that holds payload and validity under backpressure. |
| `skid` | Two-packet skid storage with registered backpressure. |
| `fifo` | An ordered queue with explicit depth and a registered ready boundary. |
| `port` | A one-cycle valid pulse for register-shaped or synchronous-memory results. |

“Transport” is the present name for this family of movement policies; the
concept is settled more firmly than the term.

Module inputs are also intended to become generic transfer endpoints. An input
may specify a transport kind when it needs a particular local policy. Without
one, the emitted module boundary still has a ready/valid handshake, so the
module accepts transfers uniformly whether the producer is a wire, register,
buffer, port, FIFO, or skid. Degenerate producers simply tie the appropriate
handshake signals to constants. The module consumes packets according to the
interface contract rather than learning how its caller stores them.

## Pipelines, FSMs, and fabrics

Pigen’s ordinary transfer assignments are enough to build an elastic datapath.
Higher-level constructs organize recurring structures around the same
semantics:

- `pipeline` describes a sequence of typed, elastic packet transforms;
- `fsm` keeps state-oriented control beside the transfers it governs;
- `fabric` connects module endpoints and generates the ready/valid interconnect.

A fabric keeps a large hookup readable:

```systemverilog
fabric sample_network #(
    parameter integer PAYLOAD_W = 32
) begin
    calibration.tx.coefficient > filter.coefficient;
    adc_left.samples.mixer -> mixer.samples.left;
    adc_right.samples.mixer --> mixer.samples.right;
endfabric
```

Pigen currently generates deterministic buffered routing and an SVG showing
the elaborated endpoints, routers, and links:

<img src="docs/fabric.svg" alt="A Pigen fabric with endpoints and routed links" width="900">

Use `--diagram PATH` to choose the SVG path or `--no-diagram` to suppress it.
Files containing several fabrics receive one diagram per fabric.

## Compiler architecture

The existing compiler is a working semantic prototype. It established the
transfer rules and generated hardware against which the language can be
tested, but much of its lowering is based on coordinated source-text
transformation.

A structured replacement frontend and compiler middle are now under active
development. They already provide immutable source management, preprocessing
with token provenance, syntax trees, scopes and symbols, structural types,
typed expression and packed-projection resolution, canonical constant
expressions, predicate analysis, and identity-based expression-use and
projected-lvalue analysis. These pieces are tested independently and are not
yet the production lowering path.

The intended compiler flow is:

```text
source and preprocessing
    -> structured syntax
    -> names, types, and expressions
    -> transfers, ownership, and clock domains
    -> pipelines, FSMs, and fabrics on shared semantics
    -> elastic RTL IR
    -> SystemVerilog
```

The next major integration step is a thin complete slice through that flow: one
module, one clocked block, one atomic transfer, its transport graph, RTL IR,
and emitted SystemVerilog. From there, each structured pass becomes
authoritative as the corresponding textual mechanism is removed. Pipelines,
FSMs, and fabrics will share the same frontend and semantic services rather
than growing private interpretations of types and expressions.

Other planned work includes richer fabric payload typing and topology control,
whole-unit ready-cycle checking, source-located aggregate-width diagnostics,
contextual expression typing, and packet-field liveness through pipelines.

## Build and verify

A C compiler and `make` are enough to build Pigen. The complete verification
suite also uses Icarus Verilog and Verilator.

```sh
make
./pigen design.pigen -o generated.sv
./pigen network.pigen -o network.sv --diagram network.svg
make verify
```

Generated designs use the small primitives in
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv).

Useful places to start:

- [`USER_GUIDE.md`](USER_GUIDE.md) is the practical walkthrough for the
  currently implemented language.
- [`SPEC.md`](SPEC.md) is the precise current language and SystemVerilog
  compatibility contract.
- [`examples/buf_pipeline.pigen`](examples/buf_pipeline.pigen) shows a basic
  elastic path.
- [`examples/join_pipeline.pigen`](examples/join_pipeline.pigen) shows an
  atomic multi-source join.
- [`examples/skid_pipeline.pigen`](examples/skid_pipeline.pigen),
  [`examples/fifo_pipeline.pigen`](examples/fifo_pipeline.pigen), and
  [`examples/port_pipeline.pigen`](examples/port_pipeline.pigen) show the
  storage choices.
- [`examples/biquad_bank.pigen`](examples/biquad_bank.pigen) is a larger
  stateful DSP example.
- [`agent_notes/COMPILER_ARCHITECTURE.md`](agent_notes/COMPILER_ARCHITECTURE.md)
  records the compiler architecture and migration plan in depth.

## Development and AI

Pigen is designed and directed by David. AI coding agents are used extensively
for implementation, testing, documentation, code review, and architectural
exploration. Their work is reviewed against the project’s intended semantics,
the specification, and the verification suite; architectural and language
decisions remain deliberate project decisions.
