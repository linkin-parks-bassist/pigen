# Pigen language specification — v1

## Purpose

Pigen is a source-to-source compiler. Its input language extends
SystemVerilog with ready/valid transport values and `pipeline` and `fabric`
top-level design units. A compiler invocation consumes `.pigen` source and
writes readable, synthesizable SystemVerilog, using the
explicit primitives in `rtl/pigen_primitives.sv` where required. It does not
change SystemVerilog simulation timing.

## Top-level design units

A `.pigen` source may contain ordinary SystemVerilog design units,
`pipeline` units, and `fabric` units together and in any top-level order.
Pipelines and fabrics lower to separately instantiable modules in the same
output.

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
source         ::= identifier "." identifier
destination    ::= identifier "." identifier
routed-arrow   ::= "->" | "-->" | "--->" | ...
```

```systemverilog
fabric system_bus #(
    parameter integer PAYLOAD_W = 32
) begin
    dma.tx > memory.dma_rx;
    cpu.tx -> memory.rx;
    debug.tx --> memory.rx;
endfabric
```

A source is one module output transport, `instance.output_port`. A destination
is one module input transport, `instance.input_port`. Ports carry no fabric
source name, destination name, route selector, address, or origin metadata.
Each output transport occurs in exactly one fabric connection and therefore
has exactly one destination. Several routed outputs may target the same input;
the fabric arbitrates them and the input remains source-blind. `>` creates an
exclusive direct link, so its destination cannot appear in another
connection. One or more dashes before `>` create a routed connection; the dash
count is retained as the connection's tier. A fabric requires at least one
connection.

One-to-many routing is represented by an explicit splitter component. The
splitter has one input transport and several distinct output transports, reads
any address or selection field from the payload, and drives exactly one output
for a unicast token. Each splitter output is then connected once by the fabric.
The fabric itself does not interpret payload addresses. Protocol-specific
addressing, including a future AXI fabric, is outside this fabric contract.

Every fabric parameter list must define `parameter integer PAYLOAD_W = ...`.
Its generated module has `clk`, `reset`, and `enable`, plus flattened
ready/valid/payload ports named by joining each endpoint's instance and port
with `__`. Each source exposes input payload/valid and output ready. Each
destination exposes output payload/valid and input ready. Route bits are
internal fabric state and are not part of either endpoint interface.

The compiler deterministically constructs a pruned balanced tree of blind
three-port routers, computes the fixed route from each output endpoint to its
connected input endpoint, and verifies forward and reverse reachability. A
readable route manifest is a comment in the same generated SystemVerilog file.

The compiler also renders the elaborated topology directly from the topology
model used for RTL emission. The SVG identifies every module instance,
transport endpoint, generated router, router port, routed physical link, direct
link, and declared connection. Output is deterministic for identical source
and compiler options. Layout is seeded from the generated router tree, relaxed
with the topology-aware spring and angular forces, separated with
footprint-aware collision passes, and refined to reduce crossings while
protecting direct links. Ports are placed on each unit toward their actual
peers, with deterministic fan-out and label-collision adjustment. With one
fabric block, its default path is the SV output
path plus `.svg`. With several fabric blocks, each path is the SV output path
plus `.` followed by the fabric name and `.svg`. `--diagram PATH` selects an
explicit path when the source has exactly one fabric; `--no-diagram` suppresses
SVG generation.

Routers inspect only the low route bit, rotate the path at each hop, buffer two
packets per ingress, and arbitrate competing inputs round robin. Every endpoint
has a two-entry queue, breaking ready timing paths while sustaining one
accepted replacement per cycle. Direct-only fabrics emit no router state.

In v0, `router_buffer_depth` and `endpoint_fifo_depth` default to two and only
the value two is accepted. `objective` accepts a nonempty expression, and
routed-arrow tiers are preserved, but both are currently topology hints only:
the emitted topology remains the deterministic balanced tree. All links in a
fabric share its `PAYLOAD_W` payload width.

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
depth expression. The reversed order is invalid. ANSI transport ports
use the same spelling, for example `input buf[7:0] in_packet` and
`output fifo[15:0][4] out_queue`; they expand to payload, `_valid`, and
`_ready` ports.

A module input transport is source-blind and a module output transport is
destination-blind. The receiving module sees only its declared payload and the
transport's ready/valid behavior. The sending module sees only its payload and
whether its output was accepted. A port does not receive the peer's transport
kind: `wire`, `logic`/`reg`, `buf`, `skid`, `fifo`, and `port` affect the local
handshake implementation, not fabric addressing or endpoint identity. The
compiler emits the degenerate valid/ready constants for `wire` and
`logic`/`reg`; a fabric connection still obeys the resulting handshake.

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

`wire`, `reg`, and `logic` are always valid. `validate(x)` on one of these
types is accepted as a no-op and produces a compiler warning; no validity
hardware is emitted. `invalidate(x)` is an error because an always-valid
transport cannot be made invalid. `flush(x)` is also invalid because these
types have no transport occupancy to empty.

An output `port` is set-and-forget: it does not hold `valid` while downstream
`ready` is low. If its one-cycle valid pulse ends without a cycle in which
`ready` is high, that token was not transferred and is lost. Use `port` only on
direct or otherwise predictably low-contention paths where this is acceptable.
When delivery must survive backpressure, the user must select an output `buf`,
`skid`, or `fifo` with sufficient capacity. Fabric topology optimization may
prefer low-contention routes for `port` outputs, but such a preference cannot
replace storage and is not a delivery guarantee.

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

Pigen preserves the original memory assignment but executes it only when
`data_in` is valid, and drives `data_in_ready` when that write can accept it.
This is intended for inferred memories and other manually declared sequential
storage.

Inside an `always_ff` block, `if (destination <= source)` is shorthand for
`if (accepts(destination, source))`: it gates the then branch on that transfer
and performs the transfer on the same accepted cycle.  Both sides must be
transport identifiers.

`validate(x)` and `invalidate(x)` are synchronous next-state writes to a local
stored transport's valid bit. They force `x` valid or invalid, respectively,
on the next cycle without waiting for downstream readiness. Like non-blocking
assignments, source order determines the next validity when a transfer and a
validity action target the same value. For a one-entry `buf`, an accepted
transfer still updates the payload when a later `invalidate` determines that
the resulting item is invalid. A later `validate` can therefore expose that
intentional payload. This makes reset seeding ordinary Pigen code:

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
to an input-owned port is accepted but has no useful local effect. On an
always-valid `wire`, `reg`, or `logic`, `validate` warns and does nothing,
while `invalidate` and `flush` are errors.

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
