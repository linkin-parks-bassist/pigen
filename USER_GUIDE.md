# Pigen user guide

Pigen lets you write synchronous ready/valid datapaths in the same shape as
the datapath itself. Name the places data may wait, connect those places with
`<=`, and keep the surrounding control logic as normal SystemVerilog. Pigen
generates the valid, ready, payload muxing, storage, and backpressure routes.

This guide is the practical starting point. [`SPEC.md`](SPEC.md) is the
precise language contract.

## Start here

Build the elaborator, turn a `.pigen` source file into SystemVerilog, then run
the full regression suite:

```sh
make
./pigen design.pigen -o design.sv
make verify
```

The generated module is ordinary synthesizable SV. Compile it with
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv), which contains the small
storage primitives Pigen instantiates.

## The central idea

Pigen transport values carry a payload plus a ready/valid contract. Inside an
`always_ff @(posedge ...)` block, this:

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

    always_ff @(posedge clk)
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
| `fifo [W:0][DEPTH] x;` | You need ordered queueing or burst absorption | Holds `DEPTH` items. Payload comes before depth. |
| `skid [W:0] x;` | You need exactly two elastic slots | A precise two-entry FIFO. |
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
always_ff @(posedge clk)
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

Storage also belongs to the module that declares it. `invalidate(x);` drops
one offered item and `flush(x);` empties local buffered storage:

```systemverilog
if (cancel_one)
    invalidate(requests);
if (restart)
    flush(requests);
```

These are deliberately local-only operations. An input transport is an
upstream-owned channel, so a child cannot invalidate or flush the parent’s
buffer. Normal acceptance still propagates through the ready chain; reaching
across the boundary to destroy someone else’s storage is rejected.

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
not rely on private `__pigen_*` implementation names. A concise Pigen-native
instance/fabric hookup syntax is intentionally not defined yet, so designs do
not get locked into a premature composition model.

## Ports and inferred memory

`port` is for results that should be sampled and offered for exactly one
cycle. It is not a lossless elastic queue: an unaccepted pulse is not retried.
That is useful for timing-friendly synchronous memory reads.

```systemverilog
reg [31:0] mem [0:255];
port [31:0] read_data;

always_ff @(posedge clk)
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

always_ff @(posedge clk)
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
2. Keep Pigen actions inside one `always_ff @(posedge clock)` domain. A
   transport binds to the first domain that uses it; crossing domains is an
   error rather than an accidental CDC.
3. Treat `valid`, `ready`, and `accepts` as control observations. Prefer a
   direct `<=` route when you simply want data to flow.
4. Compile early: `./pigen design.pigen -o design.sv`. Read the emitted RTL
   until the mapping feels unsurprising.
5. Run `make verify` before relying on a change. The examples and VCD targets
   are useful concrete references for buffer, FIFO, skid, port, guarded,
   join, output, BRAM, and FSM behavior.

## Where to look next

- [`README.md`](README.md) is the short overview and waveform index.
- [`SPEC.md`](SPEC.md) is the authoritative syntax and semantic contract.
- [`examples/`](examples) contains complete runnable pipelines and testbenches.
- [`tests/`](tests) contains focused language and lowering regressions.
- [`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv) shows the exact storage
  implementations emitted by the elaborator.
