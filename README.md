# Pigen

Pigen (short for **pi**peline **gen**erator) extends SystemVerilog with typed,
ready/valid transfers for synchronous hardware. It provides native AXI4-Stream,
AXI4-Lite and full AXI4 module interfaces and fabrics alongside the `buf`, `port`,
`fifo` and `skid` transfer types.

Declarations keep payload type, transfer behaviour and array shape explicit.
The compiler generates the storage, handshake connections and arbitration
specified by those declarations and connections, then emits synthesizable
SystemVerilog.

## Native AXI module interfaces

Declare an AXI endpoint as a transfer type:

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

This module exposes standard `s_tdata`, `s_tkeep`, `s_tlast`, `s_tvalid` and
`s_tready` pins, with corresponding `m_` pins on its output. A transfer carries
the complete beat, including its enabled sidebands. Each channel has its own
storage and handshake.

The three native families are:

| Transfer type | Channels | Configuration |
| --- | --- | --- |
| `axi4_stream` | One beat channel | DATA type, KEEP, STRB, LAST, ID, DEST and USER |
| `axi4_lite` | AW, W, B, AR and R | DATA and address widths |
| `axi4` | AW, W, B, AR and R | DATA, address and ID widths, with per-channel USER fields |

`input` declares a target/receiver interface, while `output` declares an
initiator/transmitter interface. On memory-mapped buses, B and R travel in the
reverse direction from AW, W and AR. Whole-bus child connections preserve those
directions automatically:

```systemverilog
stream_stage relay (.clk(clk), .reset(reset), .s(source), .m(destination));
```

Child bindings check the complete protocol contract, including channel fields,
widths, sidebands and timing bindings. Native typedefs and package imports let
modules share those contracts. Arrays, selected lanes, generated children and
sibling connections use the same endpoint identities.

Within a module, channels can be transferred independently:

```systemverilog
m.aw <= s.aw;
m.w  <= s.w;
s.b  <= m.b;
m.ar <= s.ar;
s.r  <= m.r;
```

Fields such as `s.aw.addr`, `s.w.data` and `s.r.resp` retain their declared types.
Named record patterns construct complete channel values. A target whose DATA
type matches `value` can reply with `s.r <= '{data:value, resp:0};`.
Channel-level `valid`, `ready` and `peek`
provide handshake and payload observations.

Native endpoints accept `buf`, `fifo[N]` or `skid` storage, with `buf` as the
default. Clock/reset bindings default to `clk` and active-high `reset`. Use
`clock=ACLK,resetn=ARESETn` for named active-low reset inputs. Channel storage
samples reset on the bound rising edge.

Separately compiled SV IP can be connected through a provisional `extern module`
contract that declares its native ports. The [external-IP example](examples/native_axi_external.pigen)
includes a matching [ordinary SV implementation](examples/native_axi_external_ip.sv).

## Memory-mapped interconnects

AXI4-Lite and AXI4 fabrics decode address windows and preserve write address/data
association through the response. Address maps accept parameters and per-instance
overrides:

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

Windows are inclusive and must be disjoint. Routing preserves global addresses
and selects the target from the starting address of each transaction. Unmapped
requests receive DECERR. Contending initiators share target arbitration, while
responses return to the correct source. Full AXI4 fabrics add source identity to
target IDs and check that the target contract provides enough ID bits.

The current memory-mapped fabric permits one outstanding write and an independent
read per target. It preserves legal bursts through completion, including response
backpressure. Width or protocol conversion requires a defined adapter.

## Stream and ordinary fabrics

AXI-Stream fabrics use stream routing and arbitration. Repeated outgoing links
select destinations through source-local `TDEST` route order, while merges retain
packet ownership and source identity. Memory-mapped and Stream fabrics currently
share the `fabric` spelling, with separate routing implementations.

An inline fabric can connect parent ports, child endpoints or internal storage:

```systemverilog
fabric begin
    producer.m -> consumer.s;
endfabric
```

`->` selects a buffered routed connection, while `>` selects an exclusive direct
connection. Typed standalone fabrics have module-style ports and parameters so
that a design can instantiate an interconnect as a child.

Ordinary elastic child ports also support inline direct links and buffered,
round-robin merges. The parameter-only standalone fabric form provides balanced
three-port router networks and SVG diagrams. Use `--diagram PATH` to select its
SVG output or `--no-diagram` to suppress diagrams.

The [native relay and switch examples](examples/native_axi_relay.pigen) cover all
three families, including parameterized memory maps. Additional examples show
[generated connections](examples/native_axi_generated.pigen),
[named contracts](examples/native_axi_named_types.pigen) and
[package contracts](examples/native_axi_packages.pigen).

## Data and transfer types

Every runtime signal has three independent components:

```text
signal = data type × transfer type × declarator shape
```

Declarations put the data type first, followed by the transfer type, name and
shape:

```systemverilog
bit[32] wire address;
int[16] buf sample;
uint[24] port result;
bit[8] logic tag;
int[16] fifo[8] pending[lanes];
```

In the last declaration, `int[16]` is signed DATA, `fifo[8]` specifies queue depth
and `[lanes]` gives the declarator shape. `uint[n]` is unsigned and `bit[n]` is a
two-state packed value. Packed records and ordinary SV integral types retain
their type, shape and signedness rules.

| Transfer type | Behaviour |
| --- | --- |
| `wire`, `reg`, `logic` | Static values whose reads do not consume tokens |
| `buf` | A one-entry elastic register |
| `port` | A registered value offered for one cycle |
| `fifo[N]` | An ordered queue with depth N |
| `skid` | A two-entry buffer with registered backpressure |

An unqualified Pigen module input has an abstract transfer type specialized by
its connection. An explicit transfer type constrains compatible peers. Internal
and output `int[n]` and `uint[n]` declarations specify their realization.

## Atomic transfers

For an elastic source and destination, `destination <= source` transfers on a
clock edge when the source is valid and the destination is ready. An expression
joins its consuming inputs into that same event:

```systemverilog
result <= sample * coefficient + bias;
```

This consumes the inputs together when `result` can accept the computed value.
Concatenations provide atomic joining, splitting and co-slicing:

```systemverilog
packet <= {header, body};
{opcode, payload} <= packet;
{upper, lower} <= {wide_packet[15:8], wide_packet[7:0]};
```

Each statement consumes its source tokens once and waits for every destination.
Related statement updates can share one transfer event through a transfer block:

```systemverilog
transfer begin
    output_packet <= result;
    history <= result;
end
```

Buffered ownership requires exclusive producers and consumers unless the compiler
can establish mutually exclusive routes. Atomic fan-out is expressed through one
co-sliced transfer or transfer block.

A complete elastic path can use several storage forms:

```systemverilog
module packet_path (
    input wire clk,
    input wire reset,
    input bit[32] buf incoming,
    output bit[32] buf outgoing
);
    bit[32] buf decoded;
    bit[32] skid timing_break;
    bit[32] fifo[8] pending;

    always @(posedge clk) begin
        decoded <= incoming;
        timing_break <= decoded;
        pending <= timing_break;
        outgoing <= pending;
    end
endmodule
```

## Transfer events and FSMs

`when` runs its body on the cycle a transfer fires:

```systemverilog
when (destination <= source) begin
    count <= count + 1;
end
```

`if` tests a computed condition, including relational `<=`. Control logic can
also use `valid`, `ready`, `accepts`, `peek`, `validate`, `invalidate` and `flush`.

FSM state labels gate ordinary statements on one clock. Reset uses ordinary
control code:

```systemverilog
fsm @(posedge clk) begin
    if (reset) goto idle;

    state idle:
        if (start) goto send;

    state send:
        when (destination <= source) goto idle;
end
```

An executed `goto` schedules the next state and terminates this FSM invocation
for the current edge. Earlier effects remain effective, and the next edge
restarts execution at the top with the destination state active.

## Elastic pipelines

A pipeline carries declared fields through elastic stage boundaries:

```systemverilog
module increment_pipeline (
    input wire clk,
    input wire reset,
    input uint[32] buf incoming,
    output uint[32] buf outgoing
);
    always @(posedge clk) begin
        pipeline packet begin
            uint[32] first;
            uint[32] second;
            stage begin first <= incoming; end
            stage begin second <= first + 32'd1; end
            yield second;
        endpipeline
        outgoing <= packet;
    end
endmodule
```

Pipeline fields use `buf` storage, which may be omitted from their declarations.
`yield` exposes the final value as a buffered source. Guards and backpressure
control stage acceptance, while `pipeline_reset(packet)` clears the bound packet
state.

## Build and verification

Build with a C17 compiler and Make:

```sh
make -j8 pigen
./pigen examples/native_axi_relay.pigen -o native_axi_relay.sv --no-diagram
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
