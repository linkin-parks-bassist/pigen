# Pigen language specification — v1

## Purpose

Pigen is a source-to-source compiler. Its input language extends
SystemVerilog with ready/valid transport values and native `pipeline` and
`fabric` top-level design units. A compiler invocation consumes `.pigen`
source and writes one readable, synthesizable SystemVerilog output, using the
explicit primitives in `rtl/pigen_primitives.sv` where required. It does not
change SystemVerilog simulation timing.

There are no secondary pipeline or fabric source formats and no separate
frontend, dispatcher, or generator. Everything in this specification is part
of the one Pigen language and compiler.

## Top-level design units

A `.pigen` source may contain ordinary SystemVerilog design units,
`pipeline` units, and `fabric` units together and in any top-level order.
Pipelines and fabrics lower to separately instantiable modules in the same
output; they are not textually inlined into an enclosing module.

`pipeline` and `fabric` begin new constructs only at source top level. Inside
ordinary `module`, `interface`, `package`, `program`, `class`, and `checker`
units those words remain ordinary SystemVerilog tokens and identifiers.

### Pipeline blocks

The pipeline grammar is:

```text
pipeline       ::= "pipeline" identifier parameters? "begin"
                     pipeline-option* stage+
                   "endpipeline"
parameters     ::= "#" "(" parameter ("," parameter)* ")"
parameter      ::= "parameter" "integer" identifier "=" expression
pipeline-option ::= "option" "skid_step" "=" nonnegative-integer ";"
stage          ::= "stage" identifier tuple "yields" tuple "begin"
                     stage-body
                   "endstage"
tuple          ::= "{" tuple-item ("," tuple-item)* "}"
tuple-item     ::= identifier | packed-type identifier
packed-type    ::= ("logic" | "wire") signedness? packed-range
signedness     ::= "signed" | "unsigned"
```

```systemverilog
pipeline mac #(
    parameter integer W = 16
) begin
    option skid_step = 4;

    stage multiply {m, x, b} yields {mx, b} begin
        logic [W-1:0] m;
        logic [W-1:0] x;
        logic [W-1:0] b;
        logic [2*W-1:0] mx = m * x;
    endstage

    stage accumulate {mx, b} yields {result} begin
        logic [2*W-1:0] sum = mx + b;
        logic [W-1:0] result = sum[W-1:0];
    endstage
endpipeline
```

Each stage is a one-entry elastic ready/valid transform. A pipeline requires
at least one stage, and stage names are unique within it. The first stage must
type every input either in its tuple or with a top-level declaration in its
body. Later bare stage inputs inherit the preceding yielded tuple's types by
position. A bare yielded value takes the type of a same-named input or body
declaration. Adjacent tuples must have equal arity and equal packed widths at
each position.

Stage signal types are packed `logic` or `wire` ranges with optional signedness.
A typed header value must not be redeclared in the body. A stage input body
declaration cannot have an initializer. Other declaration initializers and
stage statements are evaluated as combinational SystemVerilog to form the
yielded packet.

The generated pipeline interface is `clk`, `reset`, `enable`, `in_valid`,
`in_ready`, `out_valid`, `out_ready`, `packet_in`, and `packet_out`. Tuple
members are packed left to right. Parameters are forwarded to every generated
stage.

`option skid_step = N;` inserts a two-entry skid after every Nth stage and
defaults to four; zero disables periodic insertion. The option may appear at
most once. A standalone top-level `skid;` or `no_skid;` inside a stage forces
or suppresses a skid at that boundary. A stage may not contain both.

### Fabric blocks

The fabric grammar is:

```text
fabric         ::= "fabric" identifier parameters? "begin"
                     (fabric-option | connection)+
                   "endfabric"
fabric-option  ::= "option" ("router_buffer_depth" |
                              "endpoint_fifo_depth" |
                              "objective") "=" expression ";"
connection     ::= source (">" | routed-arrow) destination ";"
source         ::= identifier "." identifier "." identifier
destination    ::= identifier "." identifier ("." identifier)?
routed-arrow   ::= "->" | "-->" | "--->" | ...
```

```systemverilog
fabric system_bus #(
    parameter integer PAYLOAD_W = 32
) begin
    dma.tx.memory > memory.rx;
    cpu.tx.memory -> memory.rx.cpu;
    debug.tx.memory --> memory.rx.debug;
endfabric
```

A source is `instance.output_port.destination_handle`. A destination is
`instance.input_port` with an optional third component naming the source that
the receiver recognizes. `>` creates an exclusive direct link. One or more
dashes before `>` create a routed connection; the dash count is retained as
the connection's tier. A fabric requires at least one connection. A source
handle is bound exactly once. A direct destination cannot appear in another
connection, and recognized names at one destination are unique.

Every fabric parameter list must define `parameter integer PAYLOAD_W = ...`.
Its generated module has `clk`, `reset`, and `enable`, plus flattened
ready/valid/payload ports named by joining source or destination components
with `__`. Each source exposes input payload/valid and output ready. Each
destination exposes output payload/valid and input ready. Routed destinations
also expose a `path` output and recognized-source local parameters named
`INSTANCE__PORT__SOURCE__NAME`.

The compiler deterministically constructs a pruned balanced tree of blind
three-port routers, computes rotating source routes, verifies forward and
reverse reachability and delivered-signature uniqueness, and emits the routes
as local parameters. A readable route manifest is a comment in the same
generated SystemVerilog file; it is not a second output format.

Routers inspect only the low route bit, rotate the path at each hop, buffer two
packets per ingress, and arbitrate competing inputs round robin. Every endpoint
has a two-entry queue, breaking ready timing paths while sustaining one
accepted replacement per cycle. Direct-only fabrics emit no router or path
field.

In v0, `router_buffer_depth` and `endpoint_fifo_depth` default to two and only
the value two is accepted. `objective` accepts a nonempty expression, and
routed-arrow tiers are preserved, but both are currently topology hints only:
the emitted topology remains the deterministic balanced tree. All links in a
fabric use the single `PAYLOAD_W` payload width.

## Transport declarations

Whitespace is insignificant.  Draft v1 transport payloads are packed ranges:

```systemverilog
wire [7:0] combinational;
reg [15:0] saved;
buf signed [7:0] stage;
skid packet_t queue;
port [31:0] pulse;
fifo [7:0][4] queue4;
fifo packet_t[8] messages;
```

Packed-range payloads may use the usual `signed` or `unsigned` modifier, as in
`buf signed [15:0] sample` and `fifo signed [15:0][8] samples`.  The modifier
is preserved in generated payload declarations and primitive type parameters.

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
{destination_a, destination_b} <= {expression_a, expression_b};
_ <= expression;
peek(x)
valid(x)
ready(x)
accepts(destination, source)
invalidate(x);
validate(x);
flush(x);
```

A transport assignment is atomic.  It fires only when its structured control
path is enabled, its destination is ready, and every *distinct* transport RHS
operand is valid.  Buffered RHS operands are consumed together; their ready
routes include all other buffered operand validities, so a join never partially
consumes.  Repeated use of one operand consumes it once.

`_` is a discard destination. `_ <= x;` consumes and drops one accepted token
from `x`. In a co-sliced transfer it is an always-ready member that consumes
its corresponding RHS atomically with the real destinations, for example
`{acc_3, _} <= {acc_2 + x2 * B2, x2};`.

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

`validate(x)` and `invalidate(x)` are synchronous next-state writes to a local
transport's valid bit.  They force `x` valid or invalid, respectively, on the
next cycle without waiting for downstream readiness.  Like non-blocking
assignments, source order matters: a later accepted transfer to `x` overrides
an earlier validity action, and a later validity action overrides an earlier
transfer.  This makes reset seeding ordinary Pigen code:

```systemverilog
if (reset) begin
    delay <= '0;
    validate(delay);
end
```

The emitted primitive sets both its stored payload and valid flag in that reset
edge.  `validate` without a transfer preserves the storage's existing payload;
use it only when that payload is intentional.  For a FIFO or skid, validation
forces nonempty state (one head item when previously empty); invalidation
forces its output invalid.  `flush(x)` empties all buffered contents.  These
actions apply to local `buf`, `fifo`, `skid`, and `port` values; applying one
to an input-owned port is accepted but has no useful local effect.  They are
invalid on `wire`, `reg`, or `logic`.

Buffered values have one consumer.  Multiple writes to one destination are
allowed only when semantic control analysis proves their paths mutually
exclusive; all other fanout or producer ambiguity is an error.

A co-sliced assignment is one consuming transfer group.  Its top-level LHS
and RHS concatenations must have the same arity; every LHS item is a distinct
transport destination.  The group fires only when every destination is ready
and every distinct non-`peek` transport operand across the RHS is valid.  All
members update together and each buffered source is consumed once.  A grouped
conditional transfer, `if ({a, b} <= {ea, eb})`, tests that same all-member
acceptance event.

`peek(x)` takes exactly one transport identifier and lowers to its payload
without adding validity, readiness, or ownership.  It is therefore a raw
observation, including when `x` is invalid; guard it when token freshness is
required.  It binds `x` to the enclosing synchronous Pigen domain.

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
