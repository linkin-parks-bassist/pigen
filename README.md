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

This elastic datapath offsets each input by seven, multiplies it by three,
and queues the result:

```systemverilog
module scaled_samples
    (
        input  logic        clk,
        input  logic        reset,
        input  uint[32] buf incoming,
        output uint[32] buf outgoing
    );

    uint[32] buf     biased;
    uint[32] skid    scaled;
    uint[32] fifo[8] pending;

    always @(posedge clk) begin
        biased   <= incoming + 7;
        scaled   <= biased * 3;
        pending  <= scaled;
        outgoing <= pending;
    end
endmodule
```

Inputs `1, 2, 3` produce `24, 27, 30`, with arithmetic wrapping at 32 bits.
The skid buffer breaks the ready path, and the FIFO absorbs bursts when the
consumer pauses. Each computation advances when its operands are valid and
its destination can accept the result. Pigen generates the occupancy bits,
enables, backpressure and queue control. The [complete example](examples/scaled_samples.pigen)
can be compiled directly.

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

An atomic transfer can send each result to an output buffer and a history queue
together, even though they use different storage. A `transfer begin` block
groups the destinations into one transfer whose members run in source order:

```systemverilog
uint[32] buf     output_packet;
uint[32] fifo[8] history;

always @(posedge clk) begin
    transfer begin
        output_packet <= result;
        history      <= result;
    end
end
```

Every accepted result enters both slots on the same clock edge, consuming
`result` once. The output buffer holds its value until its reader takes it,
while the history FIFO can collect and drain values at its own pace. The two
readers therefore drain independently, but the group shares one write
acceptance: it admits the next result only when both slots can take it. Their
different handshakes compose automatically, keeping both copies in step.

By default, a buffered signal has one producer and one consumer. That rule
is what makes backpressure and token ownership unambiguous. Multiple syntactic
routes are allowed when Pigen can prove they are mutually exclusive; deliberate
fan-out belongs in one transfer block or co-sliced transfer, where its
atomicity is visible.

Ordinary `if`, `else` and `case` statements can guard transfers.
`when (destination <= source)` performs a transfer and runs its body only when
that transfer is accepted. The FSM below uses it to advance through a sequence
as values arrive and results leave. `valid`, `ready`, `accepts` and `peek`
provide explicit access to signal state when needed.

## Pipeline blocks

A pipeline uses a fixed field namespace to describe successive transformations.
Each stage reads the value of `x` at that point in the pipeline, and a transfer
into `x` defines the value passed to the following stage:

```systemverilog
pipeline packet begin
    uint[32] x;

    stage begin x <= incoming; end
    stage begin x <= x + 1; end
    stage begin x <= x * 2; end

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
exposes the final value as a buffered transfer source. Unwritten initialized
fields carry forward with the packet, and every stage advances its fields
together. The [complete example](examples/pipeline_fields.pigen) uses the same
`x` through all three stages.

## Fabrics

Hook up a whole design by describing who talks to whom. Pigen turns the connection
list into a routing fabric, generating the routers, buffers, arbitration and
backpressure paths for you:

```systemverilog
fabric controls begin
    ui.tx                -> filter.commands;
    automation.tx        -> filter.commands;
    sequencer.status     -> filter.commands;
    ui.auxiliary         -> mixer.commands;
    automation.auxiliary -> mixer.commands;
    filter.status        -> display.events;
    mixer.status         -> display.events;
endfabric
```

Here the UI, automation and sequencer all compete for `filter.commands`.
The mixer also accepts commands from multiple producers, while the display
collects status from both processing blocks. Pigen shares the routing network
between these paths and arbitrates contention as it arises; each consumer can
apply backpressure through its normal handshake.

The generated network is a balanced, pruned tree of simple, lightweight
**three-port (ternary) routing nodes**. Each node needs just one route bit to
choose its next hop, with small local round-robin arbiters sharing the links.
Hop counts grow logarithmically with the network, and buffered handshakes carry
backpressure all the way through it. Route metadata stays inside the fabric;
every child sees only its declared payload and handshake ports.

You can add a producer, connect another consumer or rearrange the design by
editing the connection list. Pigen rebuilds the routing tree inside the parent
module and emits an SVG from that same topology. `--diagram PATH` selects the
diagram path. The [complete control-network example](examples/fabric_control.pigen)
shows the surrounding modules and their ports.

Payload types and widths come from the connected ports and are checked for each
connection, so one fabric can carry differently sized packets. Each producer
port has one destination; several producers can share a consumer through
arbitration. Modules do not need to know who is on the other side. If an
application needs sender identity, it can carry that as ordinary packet data.

`->` selects buffered routing. A plain `>` makes an exclusive direct connection
that bypasses the tree. Use a splitter module with distinct output ports when
one input stream needs to select between several consumers.

## Module connections

Module ports carry their transfer type with them, so compatible child connections
carry the payload and its handshake together.

AXI declarations belong on module input/output ports. Parent logic
accesses a child boundary through `child.bus`, then constructs or unpacks channel
records using ordinary transfers. Standalone internal AXI declarations are rejected.

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
cover channel records, storage and clock/reset configuration.

## FSM blocks

An `fsm` sequences work across clock edges. This burst counter accepts a length
and emits that many values, starting at zero. Requests `3, 2` produce
`0, 1, 2, 0, 1`:

```systemverilog
module burst_counter (
    input wire clk,
    input wire reset,
    input int[16] buf length,
    output int[16] buf outgoing
);
    int[16] reg remaining;
    int[16] reg next_value;

    fsm @(posedge clk) begin
        if (reset) goto wait_request;

        state wait_request:
            when (remaining <= length) begin
                next_value <= 0;
                goto emit;
            end

        state emit: begin
            if (remaining <= 0) goto wait_request;
            when (outgoing <= next_value) begin
                next_value <= next_value + 1;
                remaining <= remaining - 1;
            end
        end
    end
endmodule
```

`wait_request` consumes a length and initializes the counter. In `emit`, the
counter advances only when `outgoing` accepts a value, so backpressure holds
both the next value and the number still to send. A nonpositive length emits
nothing. The [complete example](examples/burst_counter.pigen) can be compiled
directly.

An executed `goto` selects the next state and terminates the current invocation.
Earlier effects remain effective, and execution restarts at the top on the next
clock edge with the destination state active. The leading reset branch returns
the FSM to `wait_request`, abandoning any unfinished burst.

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
- [Pipeline stages](.knowledge/how/to/write/pipeline/stages.md)
- [FSMs and transfer events](.knowledge/how/to/write/fsm/states.md)
- [Native AXI acceptance and limits](.knowledge/what/is/the/native/axi/acceptance/status.md)
- [Core library and verification profiles](.knowledge/what/is/the/standard/cores/library.md)
- [Example designs](examples) and [native cores](cores)
