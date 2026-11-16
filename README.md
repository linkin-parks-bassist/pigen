# Pigen

Pigen (short for **pi**peline **gen**erator) is a SystemVerilog extension for
building synchronous, ready/valid hardware. It adds the transfer types `buf`,
`port`, `fifo`, `skid`, `axi4_stream`, `axi4_lite`, and `axi4` alongside the static
`wire`, `reg`, and `logic`.
Non-blocking assignment performs the appropriate handshake, so
`destination <= source` says what moves where without separately rebuilding
valid, ready, occupancy, and enable logic.

The aim is an ennicening of Verilog. The hardware is still there: widths,
clocks, storage, timing boundaries and cycle behaviour remain explicit. What
Pigen takes away is the repeated flow-control bookkeeping that tends to bury the
actual design.

## Transfer types

Every signal has a transfer type. It defines the signal's validity, readiness,
storage, consumption, and production laws. For a ready/valid channel, payload and `valid`
travel from producer to consumer while `ready` travels back. Pigen creates and
connects the handshake implicitly as part of the transfer operation.

`valid` means that the producer is offering a value. `ready` means that the
consumer can accept it. When both are high on a clock edge, the consumer
accepts the offered value. That event is a *transfer*.

The transfer type determines how the signal is stored and how its handshake
behaves. Existing Verilog signals are statics in the same model, so all of
these transfer types participate in the same operation.

| Transfer type | Valid behaviour | Ready behaviour | What it is |
| --- | --- | --- | --- |
| `wire` | Always valid | Never a destination | An always-offered combinational value |
| `reg`, `logic` | Always valid | Always ready | An always-available static signal; reading it does not consume it |
| `buf` | Valid when occupied | Ready when it can accept | A one-entry elastic register |
| `port` | A one-cycle pulse | Always ready | A registered result offered for one cycle |
| `fifo` | Valid when non-empty | Ready when non-full | An ordered, depth-N queue |
| `skid` | Valid when non-empty | Ready when non-full | A two-entry skid buffer with registered backpressure |
| `axi4_stream` | Valid when a beat is offered | Ready when a beat can be accepted | An AXI4-Stream channel with typed sidebands |
| `axi4_lite` | Per-channel validity | Per-channel readiness | An AXI4-Lite interface with AW, W, B, AR and R channels |
| `axi4` | Per-channel validity | Per-channel readiness | A full AXI4 interface with bursts, IDs and channel sidebands |

`wire`, `reg`, and `logic` are static transfer types, or statics. Their constant
laws are the trivial cases of the same transfer algebra.
The compiler folds their constant handshake behavior out of generated hardware
where the connection context permits.

## Data types

The data type says what bits a signal carries and how operations interpret
them; the transfer type says how that value exists in time. Pigen's current
primitive data types are signed `int[n]`, unsigned `uint[n]`, and one-bit
`bit`. A count after `bit` is compact packed-width notation, so `bit[8]` is a
neutral eight-bit value and `bit[32]` is a neutral 32-bit value.

Every runtime datum is a signal with three independent components:

```text
signal = data type × transfer type × declarator shape
```

Declarations put the data type first, then the transfer type and any argument,
then the signal name and its declarator shape:

```systemverilog
bit[32] wire address;
int[16] buf sample;
uint[24] port result;
bit[8] logic tag;
int[16] fifo[8] pending[lanes];
```

In the last declaration, `int[16]` is the data type, `fifo[8]` is a FIFO
transfer type with depth eight, and `[lanes]` is the declarator shape. FIFO
depth is storage behavior, not a payload or array dimension.

An unqualified Pigen module input has an abstract transfer type specialized by
its connection, while an explicit transfer type constrains what may connect to
it. Internal and output `int[n]` and `uint[n]` signals require an explicit
realization; `bit` may use its data-type-owned static default where the
specification permits. Ordinary SystemVerilog `byte` remains its distinct
signed eight-bit integral type. It is neither a Pigen primitive nor an alias
for `bit[8]`. The precise rules are in [language specification](.knowledge/what/is/the/spec.md).

## The transfer is the fundamental unit

In Pigen, `<=` means **transfer this value**.

```systemverilog
outgoing <= incoming;
```

The transfer happens on a clock edge when `incoming` is valid and `outgoing` is
ready. Otherwise nothing happens: the source is not consumed and the
destination is not changed.

This rule does not depend on whether either end is a wire, register, buffer,
port, FIFO, skid buffer or AXI channel. Those are all sources and destinations of the same
operation. They differ in their ready/valid behaviour and in the storage they
imply, but they do not require different assignment operators or different
control structures.

The transfer operation includes its handshake automatically and implicitly.
This is the central idea of Pigen. A wire and a FIFO are very different pieces
of hardware, but a transfer involving either means the same thing. The
handshake may be tied high, last one cycle, or remain stalled for ten cycles;
the surrounding logic does not need a special case.

Here is a complete elastic path:

```systemverilog
module packet_path
    (
        input  logic        clk,
        input  logic        reset,
        input  bit[32] buf  incoming,
        output bit[32] buf  outgoing
    );

    bit[32] buf     decoded;
    bit[32] skid    timing_break;
    bit[32] fifo[8] pending;

    always_ff @(posedge clk) begin
        decoded      <= incoming;
        timing_break <= decoded;
        pending      <= timing_break;
        outgoing     <= pending;
    end
endmodule
```

Each line says what moves where. Pigen generates the occupancy bits, enables,
backpressure and queue control needed to make it true.

## Transfers compose

The handshake applies to a whole expression, not one operand at a time:

```systemverilog
result <= sample * coefficient + bias;
```

This waits until all three inputs are valid and `result` is ready, then consumes
the inputs together. Pigen treats it as one atomic join.

Concatenation gives the corresponding way to join, split and co-slice values:

```systemverilog
packet <= {header, body};
{opcode, payload} <= packet;
{upper, lower} <= {wide_packet[15:8], wide_packet[7:0]};
```

The last two examples are each one transfer and one consumption of the source,
even though several destinations receive pieces of it. Every destination must
be ready before anything moves. This is how one token can be deliberately
partitioned or duplicated into correlated outputs without accidentally
creating several independent consumers.

For related updates that are clearer as statements, a transfer block says the
same thing explicitly:

```systemverilog
transfer begin
    output_packet <= result;
    history       <= result;
end
```

The block fires as a unit. Either both destinations accept the value or neither
does.

By default, a buffered signal has one producer and one consumer. That rule
is what makes backpressure and token ownership unambiguous. Multiple syntactic
routes are allowed when Pigen can prove they are mutually exclusive; deliberate
fan-out belongs in one co-sliced transfer or transfer block, where its atomicity
is visible.

## Control can ask whether a transfer happened

Ordinary `if`, `else` and `case` statements can guard transfers. Pigen also
provides a compact form for the common case where control should advance only
when a value really moved:

```systemverilog
when (destination <= source) begin
    count <= count + 1;
end
```

This performs the transfer and enters the branch on the cycle it is accepted.
It is the operational counterpart of `accepts(destination, source)`, and avoids
writing a transfer in one place and a subtly different handshake test in
another. `valid`, `ready`, `accepts`, `peek`, `validate`, `invalidate` and
`flush` are available when control logic needs to inspect or manage signal
state directly.

## Pipeline blocks

A pipeline uses a fixed field namespace to describe successive transformations.
Each stage reads the value of `x` at that point in the pipeline, and a transfer
into `x` defines the value passed to the following stage. The language model is:

```systemverilog
pipeline packet begin
    uint[32] x;

    stage begin x <= incoming; end
    stage begin x <= x + 32'd1; end
    stage begin x <= x * 32'd2; end

    yield x;
endpipeline

outgoing <= packet;
```

The same name refers to a different register at each stage boundary. This gives
procedural-looking mutability across stages while keeping one field namespace.
Within a stage, reads see its incoming packet; writes become visible in the next
stage. Several packets can occupy different stages at once, each carrying its
own value of `x`. Elastic handshakes control their progress through stalls.
Pipeline fields use `buf` storage, which their declarations may omit, and `yield`
exposes the final value as a buffered transfer source.

**Current implementation gap:** the compiler rejects the repeated-field example
above with “buffered transfer cannot source its destination.” Production currently
resolves those stage versions to one semantic signal. Distinct-name pipeline
examples run, but do not establish this defining behavior. The
[pipeline stage contract and implementation gap](.knowledge/how/do/pipeline/stage/reads/work.md)
tracks the required correction.

## Module connections and fabrics

Module ports carry their transfer type with them. Connecting compatible child
ports connects their payload and handshake signals together:

```systemverilog
fabric begin
    producer.m -> consumer.s;
endfabric
```

A fabric can connect parent ports, child endpoints and internal storage. `->`
selects buffered routing, while `>` selects an exclusive direct connection.
Ordinary elastic merges arbitrate round-robin. Typed standalone fabrics have
module-style ports and parameters so an interconnect can itself be a child.
The parameter-only standalone form also provides balanced three-port router
networks and SVG diagrams; `--diagram PATH` selects the output path.

AXI ports use the same declaration and child-connection model. Their transfer
types describe the full protocol contract, including its channel directions and
sidebands:

```systemverilog
module stream_stage (
    input wire clk,
    input wire reset,
    input uint[32] axi4_stream[keep=1,last=1] s,
    output uint[32] axi4_stream[keep=1,last=1] fifo[4] m
);
    always @(posedge clk) m <= s;
endmodule
```

Here `m <= s` transfers a complete beat, including KEEP and LAST, when both
ends can participate. The generated module exposes standard AXI pins.
Native endpoints use `buf` storage by default and also accept `fifo[N]` or
`skid`. Child connections check the declared widths, fields and timing bindings.

For memory-mapped AXI, `input` declares a target and `output` an initiator.
AW, W and AR travel toward the target, while B and R return toward the initiator.
Each channel has its own transfer event, so a relay expresses the five directions
as follows:

```systemverilog
m.aw <= s.aw;
m.w  <= s.w;
s.b  <= m.b;
m.ar <= s.ar;
s.r  <= m.r;
```

Whole-interface child bindings connect those channels together with their
correct directions. [Native declarations](.knowledge/how/to/write/native/axi/declarations.md)
cover channel records, storage and clock/reset configuration; the
[relay examples](examples/native_axi_relay.pigen) show all three families.

### Memory-mapped fabrics

AXI4-Lite and AXI4 interconnects route transactions through address windows.
Address maps accept parameters and per-instance overrides:

```systemverilog
module register_map #(
    parameter BASE0 = 32'h0000_0000,
    parameter BASE1 = 32'h0001_0000,
    parameter WINDOW_BYTES = 32'h0000_1000
) (
    input wire clk,
    input wire reset,
    input uint[32] axi4_lite[addr=32] s,
    output uint[32] axi4_lite[addr=32] m[2]
);
    fabric begin
        s -> m[0] at [BASE0:BASE0+WINDOW_BYTES-1];
        s -> m[1] at [BASE1:BASE1+WINDOW_BYTES-1];
    endfabric
endmodule
```

Windows are inclusive and disjoint. Routing preserves global addresses and
selects the target from the transaction's starting address. Arbitration preserves
write address/data association and returns responses to their source; unmapped
requests receive DECERR. The current implementation allows one outstanding write
and an independent read per target, with full AXI4 bursts held through completion.
See [address routing](.knowledge/how/does/native/memory/mapped/address/routing/work.md)
for the detailed contract.

AXI4-Stream fabrics route by TDEST and retain packet ownership through arbitration.
Stream and memory-mapped interconnects currently share the `fabric` spelling,
with distinct routing implementations for their protocol semantics.

## FSM blocks

An `fsm` describes a synchronous state register whose active state gates ordinary
statements. Transfers use the same handshake semantics as the datapath:

```systemverilog
fsm @(posedge clk) begin
    if (reset) goto idle;

    state idle:
        if (start) goto send;

    state send:
        when (destination <= source) goto idle;
end
```

An executed `goto` selects the next state and terminates the current invocation.
Earlier effects remain effective, and execution restarts at the top on the next
clock edge with the destination state active.

States, reset behaviour and transitions are explicit. The useful difference is
that the state machine speaks the same transfer language as the datapath: it
can wait on validity, readiness or an accepted transfer without rebuilding the
handshake by hand.

## SystemVerilog remains SystemVerilog

Pigen extends SystemVerilog. Modules, ports,
parameters, packed and unpacked arrays, expressions, functions, testbenches and
ordinary procedural logic remain available. Pigen constructs can sit beside
plain SystemVerilog, so a design can use them where they help without requiring
the whole codebase to be rewritten.

There is a strict compatibility contract: accepted non-Pigen SystemVerilog
retains its widths, signedness, scheduling, reset and cycle behaviour when
processed by Pigen. Pigen syntax may change freely while the project is young;
unrelated SystemVerilog does not.

## Build and verification

Build with a C17 compiler and Make:

```sh
make -j8 pigen
./pigen design.pigen -o generated.sv --no-diagram
```

Generated designs use [rtl/pigen_primitives.sv](rtl/pigen_primitives.sv). Compile
that file alongside the generated SV when simulating a design. The verification
suite requires Icarus Verilog and Verilator:

```sh
make -j8 --output-sync=target verify
```

The suite covers compiler models, emitted RTL, native module boundaries, fabric
routing and independent transaction scoreboards. `make cores-test` runs 122 core
profiles for UART, SPI, I2C, I2S, AXI-Stream, AXI4-Lite, SDRAM and AXI4, together
with invalid-configuration and checker-rejection cases. `make cores-report`
builds the interactive evidence gallery in `build/cores`, and `make cores-video`
adds rendered videos when its rendering dependencies are available.

Accepted ordinary SV retains its widths, signedness, scheduling and observable
cycles. Supported forms and remaining language-context work are described in
[capabilities and limits](.knowledge/what/is/broken.md).

## Language guides and examples

- [Language specification](.knowledge/what/is/the/spec.md)
- [Declarations](.knowledge/how/to/write/runnable/prototype/declarations.md)
- [Native AXI declarations and connections](.knowledge/how/to/write/native/axi/declarations.md)
- [Memory-mapped address routing](.knowledge/how/does/native/memory/mapped/address/routing/work.md)
- [Typed standalone fabrics](.knowledge/how/to/write/typed/standalone/fabrics.md)
- [FSMs and transfer events](.knowledge/how/to/write/fsm/states.md)
- [Native AXI acceptance and limits](.knowledge/what/is/the/native/axi/acceptance/status.md)
- [Core library and verification profiles](.knowledge/what/is/the/standard/cores/library.md)
- [Example designs](examples) and [native cores](cores)
