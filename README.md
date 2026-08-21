# Pigen

Pigen (short for **pi**peline **gen**erator) is a SystemVerilog extension for
building synchronous, flow-controlled hardware. It adds `buf`, `port`, `fifo`
and `skid` types alongside SystemVerilog's `wire`, `reg` and `logic`. These new
types bundle a data payload with automatically generated ready/valid flow
control. Pigen extends non-blocking assignment to them, so
`destination <= source` performs the appropriate handshake as part of the
assignment.

The aim is an ennicening of Verilog. The hardware is still there: widths,
clocks, storage, timing boundaries and cycle behaviour remain explicit. What
Pigen takes away is the repeated flow-control bookkeeping that tends to bury the
actual design.

Pigen compiles `.pigen` files to readable, synthesizable SystemVerilog.

## Types with built-in flow control

Each of Pigen's new types represents one logical value in the source language.
In the emitted RTL, the compiler represents it as a bundle: a data payload and
a `valid` signal travel from producer to consumer, while a `ready` signal
travels back from consumer to producer. Pigen creates and connects that bundle
automatically, along with the storage and control state required by the type.

`valid` means that the producer is offering a value. `ready` means that the
consumer can accept it. When both are high on a clock edge, the consumer
accepts the offered value. That event is a *transfer*.

The type determines how the value is stored and how its handshake behaves.
Existing Verilog values fit the same model as degenerate cases, so all of these
types participate in the same operation.

| Type | Valid behaviour | Ready behaviour | What it is |
| --- | --- | --- | --- |
| `wire` | Always valid | Never a destination | An always-offered combinational value |
| `reg`, `logic` | Always valid | Always ready | A persistent registered value; reading it does not consume it |
| `buf` | Valid when occupied | Ready when it can accept | A one-entry elastic register |
| `port` | A one-cycle pulse | Always ready | A registered result offered for one cycle |
| `fifo` | Valid when non-empty | Ready when non-full | An ordered, depth-N queue |
| `skid` | Valid when non-empty | Ready when non-full | A two-entry skid buffer with registered backpressure |

`wire`, `reg` and `logic` do not need flow-control state, but they take part in
the same transfer language as `buf`, `port`, `fifo` and `skid`. Their constant
handshake behaviour is simply folded away in the generated hardware.

## The transfer is the fundamental unit

In Pigen, `<=` means **transfer this value**.

```systemverilog
outgoing <= incoming;
```

The transfer happens on a clock edge when `incoming` is valid and `outgoing` is
ready. Otherwise nothing happens: the source is not consumed and the
destination is not changed.

This rule does not depend on whether either end is a wire, register, buffer,
port, FIFO or skid buffer. Those are all sources and destinations of the same
operation. They differ in their ready/valid behaviour and in the storage they
imply, but they do not require different assignment operators or different
control structures.

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
        input  buf [31:0]   incoming,
        output buf [31:0]   outgoing
    );

    reg  [31:0] decoded;
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

By default, a buffered transport has one producer and one consumer. That rule
is what makes backpressure and token ownership unambiguous. Multiple syntactic
routes are allowed when Pigen can prove they are mutually exclusive; deliberate
fan-out belongs in one co-sliced transfer or transfer block, where its atomicity
is visible.

## Control can ask whether a transfer happened

Ordinary `if`, `else` and `case` statements can guard transfers. Pigen also
provides a compact form for the common case where control should advance only
when a value really moved:

```systemverilog
if (destination <= source) begin
    count <= count + 1;
end
```

This performs the transfer and enters the branch on the cycle it is accepted.
It is the operational counterpart of `accepts(destination, source)`, and avoids
writing a transfer in one place and a subtly different handshake test in
another. `valid`, `ready`, `accepts`, `peek`, `validate`, `invalidate` and
`flush` are available when control logic needs to inspect or manage transport
state directly.

## Pipeline blocks

A `pipeline` block describes a sequence of elastic packet transformations:

```systemverilog
pipeline filter begin
    int[16] sample, product, result;

    stage begin
        sample <= input_sample;
    end
    stage begin
        product <= sample * coefficient;
    end
    stage begin
        result <= product + bias;
    end

    yield result;
endpipeline

output_sample <= filter;
```

Pipeline fields travel with the packet. Every `stage` introduces an elastic
boundary, and `yield` exposes the final result as another buffered transfer
source. Stalls propagate through the pipeline automatically; a stage only
advances when all of the values it needs are present and the next stage can
accept them.

The example uses Pigen's intended data-first declaration syntax, described
below. The current compiler's pipeline syntax uses ordinary SystemVerilog types
inside the block.

## Fabric blocks

A `fabric` is an inline module block, like a `pipeline` or `transfer` block. It
connects the input and output ports of child-module instances:

```systemverilog
fabric sample_path begin
    adc.samples       -> filter.samples;
    filter.results    >  dma.sample_data;
    cpu.mem_requests  -> memory.requests;
    dma.mem_requests  --> memory.requests;
    memory.responses  -> cpu.mem_responses;
endfabric
```

These are port-to-port transfers. Pigen infers and checks each payload width,
then generates the ready/valid plumbing, backpressure, pipelining and
arbitration. The child modules remain source-blind: each sees its own payload,
valid and ready, not the topology on the other side.

Dashed arrows use a balanced network of skid-buffered three-port routers, giving
logarithmic routing depth, broken ready paths and a pipelined hop structure.
The three-port shape is deliberate: once a packet arrives on one port, it has
exactly two possible onward ports, so each hop needs only one direction bit.
The complete relative address is therefore a shifting bitstring, with one bit
consumed at each router. Contention is currently arbitrated round-robin;
priorities are planned. A plain `>` is a direct exclusive connection and
bypasses the routed network.

Pigen generates an SVG from the same topology used to generate the RTL:

<img src="docs/fabric.svg" alt="A Pigen fabric with endpoints and routed links" width="900">

Use `--diagram PATH` to choose the SVG path or `--no-diagram` to suppress it.

This inline, width-inferred form is intended but not yet implemented. The
current compiler still accepts top-level, fixed-width fabric blocks; that form
will be replaced outright. Cleaner struct-like module instantiation syntax is
planned separately.

## FSM blocks

An `fsm` is not a different model of hardware. It is the familiar synchronous
state register and `case` statement, with the boilerplate pulled out and
transfers left in their natural form:

```systemverilog
fsm sender @(posedge clk) reset (reset) initial idle
begin
    state idle:
    begin
        if (start)
            goto send;
    end

    state send:
    begin
        if (destination <= source)
            goto idle;
    end
end
```

States, reset behaviour and transitions are explicit. The useful difference is
that the state machine speaks the same transfer language as the datapath: it
can wait on validity, readiness or an accepted transfer without rebuilding the
handshake by hand.

## SystemVerilog remains SystemVerilog

Pigen is an extension to SystemVerilog, not a replacement HDL. Modules, ports,
parameters, packed and unpacked arrays, expressions, functions, testbenches and
ordinary procedural logic remain available. Pigen constructs can sit beside
plain SystemVerilog, so a design can use them where they help without requiring
the whole codebase to be rewritten.

There is a strict compatibility contract: accepted non-Pigen SystemVerilog
retains its widths, signedness, scheduling, reset and cycle behaviour when
processed by Pigen. Pigen syntax may change freely while the project is young;
unrelated SystemVerilog does not.

## Data types and transport kinds

Pigen is moving toward separating what a value *is* from how it *moves*. The
intended declaration order is data type, optional transport kind, then name:

```systemverilog
int[16] buf  x;
int[16] buf  arr[8];
uint[24] fifo samples[64];
bit     port finished;
byte         tag;
```

`int[n]`, `uint[n]`, `bit` and `byte` are the first planned data types. `[n]`
means an `n`-bit value and lowers to SystemVerilog's `[n-1:0]`; array dimensions
remain after the name. Ordinary declarations such as
`logic [21:0] h[0:12];` keep their usual SystemVerilog meaning.

The transport kind is a local implementation choice. Pigen's intended module
interface reflects that: an input may optionally request a particular kind,
but an unqualified input is still emitted with payload, valid and ready. The
module does not need to know whether its caller connected a wire, a register or
a queue. It sees the same transfer interface in every case. This generic-input
form is planned rather than implemented today.

The current compiler still uses transport-first declarations such as
`buf [31:0] packet`. The data-first syntax and generic module inputs will
replace that form once their frontend and type rules are complete. Pigen is
pre-release, so there will be one clean language rather than two Pigen
dialects.

## Build and try it

A C compiler and `make` are enough to build Pigen. The full verification suite
also uses Icarus Verilog and Verilator.

```sh
make
./pigen design.pigen -o generated.sv
./pigen network.pigen -o network.sv --diagram network.svg
make verify
```

Generated designs use the small primitives in
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv).

Useful places to continue:

- [`USER_GUIDE.md`](USER_GUIDE.md) is a practical guide to the currently
  implemented language.
- [`SPEC.md`](SPEC.md) is the precise language and SystemVerilog compatibility
  contract.
- [`examples`](examples) contains small designs for buffers, joins, queues,
  ports, skid stages and larger datapaths.
- [`agent_notes/COMPILER_ARCHITECTURE.md`](agent_notes/COMPILER_ARCHITECTURE.md)
  records the compiler architecture and migration plan.

## Compiler direction

The compiler is being moved from a working source-rewriting prototype to a
structured frontend and semantic middle. Source, syntax, names, types,
expressions, clock domains, transfers and ownership are represented explicitly;
pipelines, FSMs and fabrics will all lower through those shared semantics to an
elastic RTL representation and then SystemVerilog.

That work is in progress. The current compiler is useful, but Pigen should be
treated as an experimental language whose syntax and generated implementation
will continue to change.

## Development and AI

Pigen is designed and directed by David. AI coding agents are used extensively
for implementation, testing, documentation, code review and architectural
exploration. Their work is reviewed against the intended language semantics,
the specification and the verification suite; the direction and taste of the
project remain human decisions.
