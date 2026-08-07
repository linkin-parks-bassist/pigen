# Pigen language specification — v1

## Purpose

Pigen is a C17 source-to-source elaborator.  Pigen input is SystemVerilog with
ready/valid transport values and lowers to readable synthesizable SystemVerilog
plus the explicit primitives in `rtl/pigen_primitives.sv`.  It does not change
SystemVerilog simulation timing.

## Transport declarations

Whitespace is insignificant.  Draft v1 transport payloads are packed ranges:

```systemverilog
wire [7:0] combinational;
reg [15:0] saved;
buf [7:0] stage;
skid packet_t queue;
port [31:0] pulse;
fifo [7:0][4] queue4;
fifo packet_t[8] messages;
```

`fifo` always spells payload before depth: its final bracket group is the
depth expression.  The reversed legacy order is invalid.  ANSI transport ports
use the same spelling, for example `input buf[7:0] in_packet` and
`output fifo[15:0][4] out_queue`; they expand to payload, `_valid`, and
`_ready` ports.

| Kind | valid / ready | Semantics |
| --- | --- | --- |
| `logic`, `reg` | `1` / `1` | Persistent, always-available values; reads do not consume. |
| `wire` | `1` / `0` | Always offered combinational value; not a transport destination. |
| `buf` | occupancy / elastic readiness | One-entry elastic storage. |
| `fifo` | nonempty / nonfull-or-pop | Ordered depth-N elastic storage. |
| `skid` | nonempty / nonfull-or-pop | Exact two-entry FIFO. |
| `port` | one-cycle pulse / `1` | A directly written payload register with a one-cycle Pigen-valid pulse. |

`logic` remains ordinary SystemVerilog syntax but has degenerate transport
semantics whenever it participates in a Pigen action.  Generated RTL uses
constants rather than private valid/ready nets for degenerate types.

## Transport actions

```systemverilog
destination <= expression;
valid(x)
ready(x)
accepts(destination, source)
invalidate(x);
flush(x);
```

A transport assignment is atomic.  It fires only when its structured control
path is enabled, its destination is ready, and every *distinct* transport RHS
operand is valid.  Buffered RHS operands are consumed together; their ready
routes include all other buffered operand validities, so a join never partially
consumes.  Repeated use of one operand consumes it once.

`accepts(y, x)` is exactly `ready(y) && valid(x)`.  Both arguments must be
transport identifiers.  `valid` and `ready` also require one transport
identifier.

A write to an internal `port` validates that payload for the next cycle, but
its payload assignment is emitted unconditionally.  This supports synchronous
RAM inference without a payload clock-enable, for example:

```systemverilog
always_ff @(posedge clk)
begin
    if (read_enable)
        bram_port <= mem[address];
end
```

lowers to an unconditional `bram_port <= mem[address];` clocked assignment;
`bram_port` is valid on the next cycle only when `read_enable` was true.  A
port has one Pigen producer.

An ordinary sequential storage write can consume a transport source directly:

```systemverilog
input port [7:0] data_in;
reg [7:0] mem [0:15];

always_ff @(posedge clk)
    mem[write_address] <= data_in;
```

Pigen preserves the native memory assignment but executes it only when
`data_in` is valid, and drives `data_in_ready` when that write can accept it.
This is intended for inferred memories and other manually declared sequential
storage.

Inside an `always_ff` block, `if (destination <= source)` is shorthand for
`if (accepts(destination, source))`: it gates the then branch on that transfer
and performs the transfer on the same accepted cycle.  Both sides must be
transport identifiers.

`invalidate(x)` discards one offered head item of a `buf`, `fifo`, `skid`, or
`port`; it is a no-op when empty.  `flush(x)` empties all buffered contents and
equals `invalidate` for one-entry storage.  They are synchronous actions that
do not wait for downstream readiness.  A ready downstream consumer may still
receive an already-offered item at that clock edge; clear removes residual
state.  Clear on `wire`, `reg`, or `logic` is invalid.  A clear operation may
not overlap an enqueue or dequeue of the same storage on any reachable path.

Buffered values have one consumer.  Multiple writes to one destination are
allowed only when semantic control analysis proves their paths mutually
exclusive; all other fanout or producer ambiguity is an error.

## Domains and controls

Pigen actions appear only in a synchronous one-edge `always_ff` block:

```systemverilog
always_ff @(posedge clk)
begin
    if (reset)
    begin
        // ordinary reset logic
    end
    else
    begin
        // Pigen actions and ordinary SV
    end
end
```

The event control defines a domain. A transport value binds to the domain of
its first Pigen use, and all later uses must match. A conventional top-level
`if (reset) ... else ...` branch is preserved and naturally gates actions to
the non-reset path, but it is optional. When a module declares `reset`,
generated storage connects to it; otherwise generated storage uses an inactive
reset. Unbound storage, cross-domain transport use, multiple event edges, and
asynchronous reset event controls are errors. `if`/`else` and standard `case`, `casez`, `casex`, `unique`, and
`priority` controls containing Pigen actions are parsed and retain their normal
SystemVerilog matching semantics.

## FSMs

```systemverilog
fsm sender @(posedge clk) reset (reset) initial idle
begin
    state idle:
    begin
        if (valid(in_packet))
            goto send;
    end
    state send:
    begin
        out_packet <= in_packet;
        if (accepts(out_packet, in_packet))
            goto idle;
    end
end
```

An FSM lowers to one synchronous controller and resets to its header's initial
state.  State-block actions run while their state is active.  `goto` is terminal
on its syntactic path and changes state only under its explicit enclosing
guard; otherwise state holds.  Overlapping transitions are errors unless their
paths are structurally exclusive.  A `goto` never implicitly waits for a
transport transfer; use `accepts` when it must.

## Lowering requirements

Every buffered declaration lowers to a dedicated primitive instance; storage
state machines are never inlined.  Reset clears primitive occupancy.  Payload
is stable under valid backpressure, FIFO order is preserved, simultaneous
push/pop sustains one item per cycle, and skid capacity is exactly two.

Ordinary SystemVerilog outside parsed Pigen syntax is preserved. All compiler
diagnostics identify original source file, line, and column.
