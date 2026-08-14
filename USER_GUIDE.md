# Pigen user guide

Pigen lets you write synchronous ready/valid datapaths in the same shape as
the datapath itself. Name the places data may wait, connect those places with
`<=`, and keep the surrounding control logic as normal SystemVerilog. Pigen
generates the valid, ready, payload muxing, storage, and backpressure routes.

This guide is the practical starting point. [`SPEC.md`](SPEC.md) is the
precise language contract.

## Start here

Build the elaborator, turn a `.pigen` source file into SystemVerilog, then run
the full test suite:

```sh
make
./pigen design.pigen -o design.sv
make verify
```

The generated module is ordinary synthesizable SV. Compile it with
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv), which contains the small
storage primitives Pigen instantiates.

A `.pigen` file can also declare stage-oriented `pipeline` declarations and routed
`fabric` units.

## The central idea

Pigen transport values carry a payload plus a ready/valid contract. Inside an
clocked `always @(posedge ...)` block (`always_ff` is also accepted), this:

```systemverilog
next_stage <= current_stage;
```

means “move the offered item when `current_stage` is valid and `next_stage` can
accept it.” If the destination stalls, the source item stays put. If the
destination becomes ready, the transfer happens on that clock edge. You do
not write the valid/ready boilerplate yourself.

An assignment is atomic. If its right-hand side uses multiple buffered values,
Pigen waits until every one is valid and consumes them together:

```systemverilog
sum <= left + right;
```

There is no cycle where `left` is consumed but `right` is not.

## Pipelines

A pipeline is declared inside a clocked module process. Its stages are elastic packet transforms; the procedural surface never creates a sequential multi-cycle operation.

```systemverilog
always @(posedge clk) begin
    pipeline add_then_clip begin
        logic [8:0] sum;
        logic [7:0] result;

        stage begin
            sum <= left + right;
        end
		stage begin
			result <= sum[8] ? 8'hff : sum[7:0];
		end
		yield result;
	endpipeline

    out <= add_then_clip;
end
```

## Declare a fabric block

Use a `fabric` to connect several named producers and consumers without
hand-writing a routing network:

```systemverilog
fabric command_network #(
    parameter integer PAYLOAD_W = 32
) begin
    boot.tx.command > controller.rx;
    cpu.tx.command -> controller.rx.cpu;
    debugger.tx.command --> controller.rx.debugger;
endfabric
```

The direct `>` connection is exclusive and receives its own two-entry endpoint
queue. Routed arrows use a generated tree of buffered three-port routers. The
compiler emits route constants and recognized-source constants into the fabric
module, while the boundary remains ordinary flattened ready/valid
SystemVerilog.

Compiling a source containing one fabric writes an inspectable topology diagram
next to the SV output, for example `command_network.sv.svg`. The SVG labels the
units, transport endpoints, generated router and port IDs, physical router
links, direct links, and declared connections. Its topology-aware layout places
ports toward their peers, separates node and label footprints, and reduces
wire crossings, with direct links weighted most heavily. Use `--diagram PATH` to choose
the path or `--no-diagram` to suppress it. Sources containing several fabrics
write one `OUTPUT.sv.FABRIC.svg` file per fabric.

## Your first pipeline

```systemverilog
module increment_pipe
    (
        input logic clk,
        input logic reset,
        input buf [15:0] incoming,
        output buf [15:0] outgoing
    );

    buf [15:0] work;

    always @(posedge clk)
    begin
        work <= incoming + 16'd1;
        outgoing <= work;
    end
endmodule
```

`work` is a one-item elastic stage. When `outgoing` stalls, `work` retains its
item; that backpressure reaches `incoming` automatically. Add stages by naming
them, not by rebuilding handshake logic.

## Choose the right transport storage

| Declaration | Use it when | Important property |
| --- | --- | --- |
| `buf [W:0] x;` | One elastic pipeline stage is enough | Holds one item; supports throughput of one item per cycle. |
| `fifo [W:0][DEPTH] x;` | You need ordered queueing or burst absorption | Holds `DEPTH` items and breaks combinational ready propagation. Payload comes before depth. |
| `skid [W:0] x;` | You need delayed backpressure absorbed | A two-entry, ready-registered skid buffer. |
| `port [W:0] x;` | You need a sampled one-cycle result | Valid pulses for one cycle; it does not retain an unaccepted item. |
| `wire`, `reg`, `logic` | The value is always available or is ordinary control/state | They have degenerate transport semantics when used in a Pigen action. |

For example:

```systemverilog
buf  [31:0] decode;
fifo [31:0][16] requests;
skid [31:0] response_stage;
port [31:0] bram_result;
```

`fifo[7:0][4] queue;` is correct. The older reversed spelling is not.

## Control flow and transfer flow

Use normal SV controls around transfers. A guarded route stays inactive until
its guard is true; meanwhile the source retains its item.

```systemverilog
always @(posedge clk)
begin
    if (issue_request)
        requests <= request;

    if (select_a)
        result <= a;
    else
        result <= b;
end
```

`if`/`else`, `case`, `casez`, `casex`, `unique case`, and `priority case` are
understood for transport-route exclusivity. Two writers to the same transport
are permitted only when their control paths are provably disjoint, as in the
`if`/`else` above.

When control needs to observe the handshake, use:

```systemverilog
if (valid(requests))
    busy <= 1'b1;

if (ready(result))
    result_slot_available <= 1'b1;

if (accepts(result, requests))
    accepted_count <= accepted_count + 1'b1;
```

`accepts(destination, source)` means destination-ready and source-valid.

For the common “try this transfer and continue only if it worked” pattern,
write the transfer as the condition:

```systemverilog
if (result <= requests)
    accepted_count <= accepted_count + 1'b1;
```

That condition is equivalent to `accepts(result, requests)` and performs the
same transfer on the accepted cycle. Its two sides are transport identifiers.

## Ownership: the rule that keeps flow simple

A buffered transport has one consumer. This makes ownership explicit and lets
Pigen construct one unambiguous ready route. If you need to send one item to
two places, choose the behavior intentionally: duplicate the payload into two
destinations, add a fork protocol, or make one consumer own the decision.

A transport cannot consume itself in the transfer that writes it:

```systemverilog
buf [7:0] count;
count <= count + 1'b1; // error: circular ready dependency
```

Use ordinary register state or a distinct FIFO or skid stage for feedback.
Pigen defines both FIFO and skid input readiness from registered occupancy;
neither propagates output readiness combinationally to its input.

Storage also belongs to the module that declares it. `validate(x);` and
`invalidate(x);` explicitly set a local transport valid or invalid on the next
clock edge.  They compose in source order with transfers, just like normal
non-blocking assignments.  This is the normal way to seed elastic feedback
state, including during reset:

```systemverilog
if (reset) begin
    x1 <= '0; validate(x1);
    x2 <= '0; validate(x2);
end
```

`x1` and `x2` are now zero-valued valid buffer tokens after reset, so downstream
joins do not stall waiting for history.  `invalidate(x);` forces a transport
invalid and `flush(x);` empties local buffered storage:

```systemverilog
if (cancel_one)
    invalidate(requests);
if (restart)
    flush(requests);
```

`wire`, `reg`, and `logic` are always valid, so validating one is a warned
no-op. Invalidating or flushing one is an error.

These are deliberately local-only operations. Normal acceptance still
propagates through ready chains; use validity actions on storage your module
owns rather than reaching into an upstream buffer.

`_` is an explicit discard destination. It consumes a token without creating
storage, and is particularly useful in co-sliced transfers:

```systemverilog
{acc_3, _} <= {acc_2 + x2 * B2, x2};
```

Here `acc_3` advances and the old `x2` token is retired atomically.

## Module boundaries

Use the same transport spelling in an ANSI module port:

```systemverilog
module worker
    (
        input logic clk,
        input logic reset,
        input buf  [31:0] request,
        output fifo [31:0][8] response
    );
    // ...
endmodule
```

Pigen expands each transport boundary into ordinary SV payload, `_valid`, and
`_ready` ports. An `input buf`, `input fifo`, or `input skid` is an
upstream-owned channel view: no duplicate child buffer is created, and the
child’s ready signal propagates to the upstream owner. An output transport is
stored inside the child and presented to its parent using the same contract.

This gives normal data flow a transparent ready chain across hierarchy. Clear
and invalidate do not cross that boundary.

Today, module instantiation itself remains standard SystemVerilog. Connect
the generated public payload/valid/ready ports with explicit SV signals; do
not rely on private `__pigen_*` implementation names. A concise Pigen
instance/fabric hookup syntax is intentionally not defined yet, so designs do
not get locked into a premature composition model.

## Ports and inferred memory

`port` is for results that should be sampled and offered for exactly one
cycle. It is not a lossless elastic queue: an unaccepted pulse is not retried.
That is useful for timing-friendly synchronous memory reads.

```systemverilog
reg [31:0] mem [0:255];
port [31:0] read_data;

always @(posedge clk)
begin
    if (read_enable)
        read_data <= mem[read_address];
end
```

Pigen emits the payload assignment as an unconditional clocked write, keeping
the familiar `read_data <= mem[read_address];` shape that RAM inference tools
expect. The Pigen valid pulse is asserted only when `read_enable` was true;
consumers see that pulse on the following cycle.

You can consume an input transport directly in a conventional memory write:

```systemverilog
input port [31:0] write_data;
reg [31:0] mem [0:255];

always @(posedge clk)
    mem[write_address] <= write_data;
```

Pigen preserves the memory assignment, performs it only when `write_data` is
valid, and drives `write_data_ready` when the write can accept the item. See
[`tests/port_bram.pigen`](tests/port_bram.pigen) and run
`make bram-waveform` for the complete read/write example and VCD.

## FSMs

Use an `fsm` when named sequential states make the control story clearer:

```systemverilog
fsm sender @(posedge clk) reset (reset) initial idle
begin
    state idle:
    begin
        if (valid(request))
            goto send;
    end
    state send:
    begin
        if (response <= request)
            goto idle;
    end
end
```

State-block actions run only in their active state. `goto` changes state under
its enclosing guard; it does not implicitly wait for a transfer, so use
`accepts(...)` or `if (destination <= source)` when that is what you mean.

## Practical workflow

1. Start with named `buf` stages. Add a `fifo` only where you need deliberate
   queue depth, and a `skid` where two slots are exactly what the timing path
   needs.
2. Keep Pigen actions inside one `always @(posedge clock)` domain. `always_ff`
   is also accepted. A
   transport binds to the first domain that uses it; crossing domains is an
   error rather than an accidental CDC.
3. Treat `valid`, `ready`, and `accepts` as control observations. Prefer a
   direct `<=` route when you simply want data to flow.
4. Compile early: `./pigen design.pigen -o design.sv`. Read the emitted RTL
   until the mapping feels unsurprising.
5. Run `make verify` before relying on a change. The examples and VCD targets
   are useful concrete references for buffer, FIFO, skid, port, guarded,
   join, output, BRAM, and FSM behavior.

## Fixed-point and ordinary state: two current edges

Pigen transports accept packed SystemVerilog signedness modifiers.  Write
`buf signed [23:0] sample` (or `fifo signed [23:0][4] samples`) when the
payload is signed; the generated payload signal and primitive type parameter
remain signed.  Use `$signed(...)` only when deliberately reinterpreting an
otherwise unsigned expression.

The single-consumer rule applies even when the second use looks like an
ordinary state update.  In particular, this is two consumers of `sample` and
is rejected:

```systemverilog
packet <= sample;
sample_history <= sample;
```

Pack every value that needs to travel together into one transport item, or use
the explicit co-sliced transfer described below.  Do not depend on generated
`__pigen_*` signal names: they are implementation details, not a module
interface.

## Copy a token deliberately; inspect it with `peek`

Use a co-sliced transfer when one accepted token must update several related
destinations atomically.  Every destination must be ready; each distinct RHS
transport is valid and consumed once, even if it appears in more than one RHS
expression:

```systemverilog
if ({packet, history} <= {{sample, history}, peek(sample)})
    previous_history <= history;
```

The braces describe ordinary packed bit streams. Only the total LHS and RHS
widths must match; item counts and individual widths may differ. The leftmost
destination receives the most-significant portion. Thus `{header, body} <=
packet` splits one packet atomically, while `packet <= {header, body}` joins two
sources atomically. Nested braces are ordinary packing, so the example stores
an assembled packet and the raw sample on the same edge.
`peek(sample)` is a raw, non-consuming payload read: it adds neither valid nor
ready logic.  Use it under `valid(...)`, `accepts(...)`, or a grouped transfer
when the sampled value must correspond to a real token.

A payload slice still consumes its complete base token. For example,
`byte <= packet[7:0]` invalidates the whole accepted `packet`, not just its low
byte. To split one token safely, put every projection in the same transfer:

```systemverilog
{upper, lower} <= {packet[15:8], packet[7:0]};
```

Constants contribute payload bits but no handshake state; as sources they are
always valid and are never consumed.

Ordinary `reg`/`logic` state, packed members, and memory elements can be mixed
into the destination concatenation. They are always-ready members and update
only when the same atomic transfer fires:

```systemverilog
{packet_out, status[3:0], history[index]} <= source_packet;
```

A buffered destination must still be written whole; a partial write would give
its one valid bit no coherent packet meaning. Pure ordinary-SV assignments are
not subjected to transport ownership rules, so normal source-ordered
nonblocking assignment behavior remains intact.

## Where to look next

- [`README.md`](README.md) is the short overview and waveform index.
- [`SPEC.md`](SPEC.md) is the authoritative syntax and semantic contract.
- [`examples/`](examples) contains complete runnable pipelines and testbenches.
- [`tests/`](tests) contains focused language and lowering regressions.
- [`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv) shows the exact storage
  implementations emitted by the elaborator.
