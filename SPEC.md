# Pigen language specification — v1

## Purpose

Pigen is a source-to-source compiler. Its input language extends
SystemVerilog with ready/valid transport values and `pipeline` and `fabric`
top-level design units. A compiler invocation consumes `.pigen` source and
writes readable, synthesizable SystemVerilog, using the
explicit primitives in `rtl/pigen_primitives.sv` where required. It does not
change SystemVerilog simulation timing.

## SystemVerilog compatibility contract

Pigen is an extension layer over SystemVerilog, not a replacement language.
An existing SystemVerilog codebase must be able to pass through Pigen and retain
the same observable behavior, then adopt Pigen constructs incrementally. In
particular:

1. Source that does not use Pigen syntax has ordinary SystemVerilog meaning.
   Pigen may reformat or mechanically lower it, but must preserve its ports,
   values, widths, signedness, event scheduling, reset behavior, and externally
   observable cycle behavior.
2. Adding Pigen constructs in one part of a design must not reinterpret
   unrelated SystemVerilog elsewhere. Extension syntax is recognized only in
   its specified grammatical and semantic contexts.
3. If Pigen cannot preserve the meaning of an accepted SystemVerilog construct,
   it must issue a source-located diagnostic rather than silently emit different
   behavior.
4. The language may deliberately reject a SystemVerilog construct when the
   project explicitly chooses a safer subset. Every such restriction is part of
   this specification and requires a direct diagnostic; an accidental parser or
   lowering limitation is not a language restriction.
5. Textual identity of the generated file is not required. Behavioral
   compatibility is required, subject only to the explicitly documented
   restrictions above.

This compatibility contract is an overarching design and release requirement.
New Pigen features and syntax decisions are subordinate to it.

## Top-level design units

A `.pigen` source may contain ordinary SystemVerilog design units and `fabric`
units together and in any top-level order. The preferred pipeline form is an
inline declaration inside an enclosing clocked `always` block; it elaborates
to storage and logic in the parent module, not a generated child module.

The older top-level `pipeline` unit remains accepted as a compatibility form
while existing sources migrate. New designs should use the inline form below.

`fabric` begins a new construct only at source top level. Inside ordinary
`module`, `interface`, `package`, `program`, `class`, and `checker` units those
words remain ordinary SystemVerilog tokens and identifiers. Inside a module,
`pipeline name {` begins an inline pipeline declaration only inside a supported
clocked `always`/`always_ff` block; all other uses of `pipeline` retain their
ordinary SystemVerilog meaning.

### Inline pipeline blocks

```text
inline-pipeline ::= "pipeline" identifier packed-expression "yields"
                    packed-expression "begin" pipeline-item* stage+
                    "endpipeline"
pipeline-item   ::= packed-type identifier-list ";"
stage           ::= "stage" identifier? packed-list "yields" packed-list
                    (";" | "begin" stage-body "end")
packed-list     ::= "{" packed-item ("," packed-item)* "}"
packed-item     ::= expression | packed-type identifier
pipeline-reset  ::= ("pipe_reset" | "pipeline_reset") "(" identifier ");"
```

The header input packing is read from enclosing module scope. The header output
packing is one atomic output packet; its named members are readable by later
statements in the enclosing module. Stage names and declarations are private to
the pipeline, and each stage has its own combinational scope. A pipeline-level
declaration establishes a reusable type association; the same association may
be introduced inline in either packed list.

The enclosing clocked `always` supplies the clock domain. The complete guard
around the declaration is the pipeline enable: when false, all stage storage is
held and no input or output handshake occurs. `pipe_reset` and
`pipeline_reset` are declarative aliases. Their full procedural guard directly
resets every generated valid bit on that edge; repeated reset bindings combine
with logical OR. In the absence of an explicit binding, a parent port named
`reset` is used as the conventional synchronous reset; otherwise the inline
pipeline is intentionally unreset.

Curly braces always retain normal packed-concatenation semantics. A stage or
transfer is atomic over its complete packing, so a source transport is consumed
only when all non-degenerate destinations are ready. Stage bodies are currently
combinational; nested transport operations and `stall();` are reserved future
syntax, and no implicit `busy` mechanism exists.

Adjacent stage packings may have different member counts and member widths.
They are connected by their complete bit stream, leftmost member most
significant, and must have equal total `$bits` width; Pigen emits a generated
elaboration check for each boundary. An expression whose type cannot be
inferred positionally across an equal-arity boundary must first be assigned to
an explicitly typed pipeline name before such repartitioning.

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
| `fifo` | nonempty / nonfull | Ordered depth-N storage and a combinational ready-chain break. |
| `skid` | nonempty / nonfull | Exact two-entry skid buffer with registered backpressure. |
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

All grammar in this section is token-based. The tokenizer throws away spaces,
tabs, comments, and newlines, so formatting shown in examples is never required
syntax.

```systemverilog
destination <= expression;
{destination_a, destination_b} <= {expression_a, expression_b};
transfer begin
    destination_a <= expression_a;
    destination_b <= expression_b;
end
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

The LHS may be one complete buffered transport destination, an ordinary
SystemVerilog variable lvalue, or a concatenation mixing those forms. Buffered
destinations must be written whole because their valid bit describes the whole
packet. Degenerate `reg`/`logic` and ordinary state may use normal SV slices,
members, array elements, indexed part-selects, and nested lvalue
concatenations. The RHS is one ordinary SystemVerilog value expression and may
itself be a concatenation. Assignment uses the ordinary
concatenation bit stream: the leftmost destination receives the most
significant portion and the rightmost destination receives the least
significant portion.  Only the aggregate widths must match; top-level item
counts and individual item widths need not match.  Thus all of these are the
same kind of atomic transfer:

```systemverilog
x <= {a, b};
{x, y} <= a;
{x, y} <= {a, b};
```

The explicit atomic-block spelling is:

```text
atomic-transfer ::= "transfer" "begin" transfer-member+ "end"
transfer-member ::= destination-expression "<=" source-expression ";"
```

It is exact semantic sugar for concatenating the member destinations and
sources in source order. For example:

```systemverilog
transfer begin
    output_packet <= result;
    state_mem[handle] <= next_state;
end
```

is equivalent to:

```systemverilog
{output_packet, state_mem[handle]} <= {result, next_state};
```

The block has one guard, one aggregate width check, one ready/valid decision,
and one fire event. All members update or none do. Individual member widths
and arities may differ; only the two flattened aggregate widths must match.
The first version permits only transfer members directly inside the block;
control flow belongs around the block rather than inside it.

Any transport read needed to evaluate a member participates in the block's
validity and ownership calculation, including a transport projection used in
an lvalue address or select. Thus `state_mem[handle]` requires `handle` to be
valid. If `handle`, `result`, and `next_state` are projections of one packet,
the block is one deduplicated consumer of that complete packet, not three
consumers. Constants and degenerate values retain their normal rules.

`transfer` is contextual: it introduces this extension only when followed by
the complete `transfer begin ... end` form in a clocked procedural block.
Otherwise it remains an ordinary SystemVerilog identifier.

The complete statement has one destination set and one consuming source set.
It fires when every destination is ready and every distinct buffered base
transport read anywhere in the RHS is valid.  Constants add no validity or
consumption dependency: as sources they behave like always-valid wires and
have no readiness state.

A select or member access projects bits from a transport payload; it does not
create a smaller transport.  An unpeeked read such as `x <= a[7:0]` therefore
consumes the complete token held by `a` when it fires.  Repeating projections
of the same base in one statement still consumes that base once:

```systemverilog
{high, low} <= {a[15:8], a[7:0]};
```

This is one consumer of `a`, and neither destination can accept a different
token from the other.  The corresponding two separate statements are two
consumers and are rejected unless their guards are proven mutually exclusive.

If a statement contains any buffered source or destination, its ordinary
`reg`/`logic`/memory lvalue members participate in the same atomic event. They
are always ready, add no validity state, and update only when the complete
transfer fires. A pure degenerate statement containing no buffered transport is
ordinary SystemVerilog and retains normal procedural semantics, including
source-ordered nonblocking assignments to the same variable. The transport
single-consumer and producer-exclusivity rules apply only to non-degenerate
storage. A procedural `wire` destination is rejected, consistently with normal
SystemVerilog variable-assignment rules.

Concatenated and projected transfers require equal aggregate LHS and RHS
widths. Pigen emits a constant `$bits` elaboration check alongside the lowered
assignment, allowing the SystemVerilog tool to resolve typedefs, parameters,
members, and indexed selects with its own type rules. Plain whole-value
assignments retain ordinary SV assignment sizing and signedness conversion.

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
always @(posedge clk)
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

always @(posedge clk)
    mem[write_address] <= data_in;
```

Pigen preserves the original memory assignment but executes it only when
`data_in` is valid, and drives `data_in_ready` when that write can accept it.
This is intended for inferred memories and other manually declared sequential
storage.

Inside a clocked `always` or `always_ff` block,
`if (destination <= source)` is shorthand for
`if (accepts(destination, source))`: it gates the then branch on that transfer
and performs the transfer on the same accepted cycle.  Both sides must be
transport identifiers.

`validate(x)` and `invalidate(x)` are synchronous next-state writes to a local
stored transport's valid bit. They force `x` valid or invalid, respectively,
on the next cycle without waiting for downstream readiness. Like non-blocking
assignments, source order matters: a later accepted transfer to `x` overrides
an earlier validity action, and a later validity action overrides an earlier
transfer. This makes reset seeding ordinary Pigen code:

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

A consuming transfer group may not read one of its own buffered destinations.
For example, `x <= x + 1;` is an error: lowering it as an ordinary transport
transfer would connect `x`'s output-ready route to its own input-ready route.
The same rule applies when the self-consumption crosses members of a co-sliced
group. Feedback must instead pass through a distinct element that breaks the
combinational ready dependency, such as ordinary register state or a storage
stage specifically implemented as a ready-chain break. More generally, a
combinational ready-dependency cycle is invalid even when it spans several
connections.

A co-sliced assignment is one consuming transfer group. Every buffered LHS
item is a distinct, complete transport destination; degenerate and ordinary
lvalue items may be projected. The RHS is flattened and repartitioned by LHS
widths using ordinary SystemVerilog concatenation order;
the two sides need equal aggregate width, not equal arity. The group fires only
when every destination is ready and every distinct non-`peek` base transport
across the RHS is valid. All members update together and each buffered source
is consumed once. A grouped conditional transfer,
`if ({a, b} <= expression)`, tests that same all-member acceptance event.

`peek(x)` takes exactly one transport identifier and lowers to its payload
without adding validity, readiness, or ownership.  It is therefore a raw
observation, including when `x` is invalid; guard it when token freshness is
required.  It binds `x` to the enclosing synchronous Pigen domain.

## Domains and controls

Pigen actions appear only in a synchronous one-edge `always` or `always_ff`
block. Plain `always` is the preferred source spelling; generated sequential
hardware may use `always_ff`:

```systemverilog
always @(posedge clk)
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
state machines are never inlined. Reset clears primitive occupancy. Payload
is stable under valid backpressure, FIFO order is preserved, and skid capacity
is exactly two. FIFO and skid input readiness depend only on their registered
occupancy, never combinationally on downstream readiness. They sustain
simultaneous push/pop while nonfull; after a full queue pops, input readiness
returns from the new occupancy on the following cycle.

Ordinary SystemVerilog outside parsed Pigen syntax obeys the compatibility
contract above. All compiler diagnostics identify original source file, line,
and column.
