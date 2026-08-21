# Pigen language specification — v1

## Purpose

Pigen is a source-to-source compiler. Its input language extends SystemVerilog
with first-class ready/valid transfers, explicit data and transfer types, and
inline `pipeline`, `transfer`, `fabric`, and `fsm` blocks. A compiler invocation
consumes `.pigen` source and writes readable, synthesizable SystemVerilog, using
the explicit primitives in `rtl/pigen_primitives.sv` where required. It does
not change SystemVerilog simulation timing.

This document specifies the intended v1 language. The compiler is pre-release
and is being moved onto a structured implementation; it does not yet accept
every form specified here. In particular, data-first declarations, generic
input endpoints, and inline width-inferred fabrics are not implemented. The
currently accepted transfer-type-first declarations and top-level fixed-width
fabric blocks are prototype syntax to be replaced, not alternate language
forms. Implementation lag does not change the language contract.

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

There is one deliberate declaration-syntax exception: a colonless bracket in a
declaration-dimension position is Pigen count notation, as specified below.
This intentionally changes the meaning which ordinary SystemVerilog would give
that declaration dimension. Explicit colon-bearing SystemVerilog ranges and
all expression selects retain their ordinary meaning.

## Design-unit and contextual syntax

A `.pigen` source contains ordinary SystemVerilog design units. Pipelines,
transfers, fabrics, and FSMs elaborate inside their owning module rather than
becoming separate public design units.

A pipeline is declared inside a supported clocked `always`/`always_ff` block
and elaborates to storage and logic in its parent module. A fabric is a module
item and connects ports of child instances owned by that module. `transfer`
introduces an atomic statement block only in a supported clocked process. These
words remain ordinary SystemVerilog identifiers outside their complete
specified contextual forms.

### Pipelines

```text
pipeline       ::= "pipeline" identifier "begin"
                      pipeline-declaration* stage+ "yield" expression ";"
                    "endpipeline"
pipeline-declaration ::= data-type identifier-list ";"
stage           ::= "stage" identifier? "begin" stage-item* "end"
stage-item      ::= declaration | assignment
pipeline-reset  ::= ("pipe_reset" | "pipeline_reset") "(" identifier ");"
```

This is procedural surface syntax for an elaborated elastic pipeline, not an
untimed sequential program. Pipeline-local declarations create mutable
travelling packet fields. A stage's `field <= expression;` computes its next
packet field, while every pipeline-field read observes that stage's immutable
incoming packet. A transfer becomes observable only in the following stage; it
does not introduce a cycle beyond the elastic stage boundary.
Each stage has a private local scope. Lookup is stage-local, then
pipeline-local, then enclosing module scope; a shadowing declaration is warned.
The first stage takes only enclosing module signals and stage-local
combinational signals. Every enclosing signal whose transfer law consumes a
read is an atomic stage input: the stage waits for all such inputs and consumes
them on its advance, under the complete enclosing procedural guard of the
pipeline.

`yield expression;` appears exactly once after the final stage. It evaluates
from that stage's outgoing packet, so all transfers in the final stage are
visible in the yielded value. It makes the pipeline identifier itself the final
module-scope buffered output
packet. It is consumed by ordinary Pigen transfers, for example `out <= pipe;`
or `{hi, lo} <= {pipe[15:8], pipe[7:0]};`. It has the ordinary sole-consumer,
valid, ready, and same-cycle replenishment semantics of `buf`.

`export` is reserved but deliberately not accepted. Its intended purpose is a
one-cycle module-scope `port` pulse from an earlier stage; its scope, type, and
collision semantics remain to be specified.

The enclosing clocked `always` supplies the clock domain. The complete guard
around the declaration is the pipeline enable: when false, all stage storage is
held and no input or output handshake occurs. `pipe_reset` and
`pipeline_reset` are declarative aliases. Their full procedural guard directly
resets every generated valid bit on that edge; repeated reset bindings combine
with logical OR. In the absence of an explicit binding, a parent port named
`reset` is used as the conventional synchronous reset; otherwise the pipeline
is intentionally unreset.

Curly braces retain normal packed-concatenation semantics. A stage advances
atomically over its inferred packet and source signals. Stage-local wires
use ordinary combinational `=` definitions. `wire <= expression` is a fatal
error: Pigen `<=` is a transfer and a wire is never ready. Nested signal
operations and `stall();` are reserved future syntax, and no implicit `busy`
mechanism exists.

Adjacent stage packings may have different member counts and member widths.
They are connected by their complete bit stream, leftmost member most
significant, and must have equal total `$bits` width; Pigen emits a generated
elaboration check for each boundary. An expression whose type cannot be
inferred positionally across an equal-arity boundary must first be assigned to
an explicitly typed pipeline name before such repartitioning.

### Fabric blocks

The intended fabric grammar is:

```text
fabric         ::= "fabric" identifier "begin" connection+ "endfabric"
connection     ::= source (">" | routed-arrow) destination ";"
source         ::= identifier "." identifier
destination    ::= identifier "." identifier
routed-arrow   ::= "->" | "-->" | "--->" | ...
```

```systemverilog
fabric sample_path begin
    adc.samples       -> filter.samples;
    filter.results    >  dma.sample_data;
    cpu.mem_requests  -> memory.requests;
    dma.mem_requests  --> memory.requests;
    memory.responses  -> cpu.mem_responses;
endfabric
```

The block is a module item. A source resolves to one output-port identity on a
child instance of the enclosing module; a destination resolves to one input-port
identity on another child instance. The complete payload types of the two ports
must be transfer-compatible. Pigen infers and checks those types and widths per
connection; there is no fabric-wide payload-width parameter.

Ordinary SystemVerilog child instantiation remains accepted. A cleaner
struct-like Pigen instantiation surface is planned but not yet specified.
Fabric semantics depend only on resolved instance and port identities, so that
surface choice cannot alter routing or require fabric-specific name parsing.

Ports carry no fabric source name, destination name, route selector, address,
or origin metadata. Each output endpoint occurs in exactly one fabric
connection and therefore has exactly one destination. Several routed outputs
may target the same input; the fabric arbitrates them and the input remains
source-blind. `>` creates an exclusive direct link which bypasses the routed
network, so its destination cannot occur in another connection. One or more
dashes before `>` create a routed connection. The dash count is retained as a
connection tier for planned priority and topology controls; until those
controls are specified, routed arbitration is round-robin.

One-to-many routing is represented by an explicit splitter child. The splitter
has one input and several distinct outputs, reads any protocol-specific address
or selection field from the payload, and drives exactly one output for a
unicast token. Each output is then connected once by the fabric. The fabric
itself does not interpret payload addresses; protocol-specific addressing,
including a future AXI fabric, is outside this contract.

The compiler deterministically constructs a pruned balanced tree of blind
three-port routers, computes the fixed route from each output endpoint to its
connected input endpoint, and verifies forward and reverse reachability. The
balanced topology gives logarithmic router depth in the number of endpoints.

The three-port shape is deliberate. Once a packet arrives on one port, a
U-turn is excluded and exactly two onward ports remain. One bit therefore
specifies the next relative direction. A route is a compact shifting bitstring:
each router inspects and advances one bit rather than comparing a global
destination address. Route bits are internal fabric state and never appear in
child-module interfaces.

The compiler renders the elaborated topology directly from the same topology
model used for RTL emission. The SVG identifies every module instance,
signal endpoint, generated router, router port, routed physical link, direct
link, and declared connection. Output is deterministic for identical source
and compiler options. Layout is seeded from the generated router tree, relaxed
with topology-aware forces, separated with footprint-aware collision passes,
and refined to reduce crossings while protecting direct links. With one fabric
block, its default path is the SV output path plus `.svg`. With several fabric
blocks, each path is the SV output path plus `.` followed by the fabric name
and `.svg`. `--diagram PATH` selects an explicit path when the source has
exactly one fabric; `--no-diagram` suppresses SVG generation.

Every routed endpoint and router ingress has two-entry skid-like storage.
Ready therefore depends on registered occupancy rather than propagating through
the complete network, while each hop can accept one replacement packet per
cycle. Backpressure, route progression, and round-robin contention arbitration
are generated automatically. Direct-only fabrics emit no router state.

## Signal declarations

Data type and transfer type are independent axes. Their declaration order is:

```text
data-type  transfer-type  declarator
```

Every signal has an independent data type, transfer type, and declarator shape.
The initial Pigen data types are:

- `int[n]`: an `n`-bit signed integer;
- `uint[n]`: an `n`-bit unsigned integer;
- `bit`: a one-bit value;
- `byte`: an unsigned eight-bit bit-vector.

`byte` is not an integer and has no arithmetic signedness. The two-state or
four-state policy of the initial types is not yet fixed. Until it is, programs
whose meaning depends on that property are outside the accepted v1 subset. The
data-type algebra is open to later scalar, aggregate, enum, and user-defined
types.

The concrete transfer types are `wire`, `reg`, `logic`, `buf`, `port`, `fifo`,
and `skid`. `wire`, `reg`, and `logic` are static transfer types, called
**statics** informally. Their valid and ready behavior is constant, but they are
full members of the same transfer-type algebra. Static behavior is the trivial
case of the general transfer behavior, not an exception to it. The complement
may be called dynamic when contrast is necessary, but ordinary writing simply
says transfer type. FIFO capacity requires a transfer-type parameter; its
data-first surface spelling remains to be specified.

Examples of the declaration order are:

```systemverilog
int[16] buf sample;
uint[24] skid response;
bit port finished;
int[16] buf lanes[8];
```

A colonless bracket in a declaration-dimension position is a count. It lowers
structurally as `[X]` to the SystemVerilog range `[X-1:0]`. On a data type it
sets packed value width; after a declarator it sets array extent. This rule does
not apply to expression indexing: `value[3]` remains a one-bit select. An
explicit colon-bearing range retains ordinary SystemVerilog direction and
meaning, including `logic [21:0] h[0:12];`.

A signal's declarator shape is the ordered list of dimensions written after its
declarator. The scalar shape is the empty list. A colonless count and an
explicit range remain distinct structural forms even when they describe the
same number of elements. Whole-signal expressions carry the same shape as the
signal they reference, so shape compatibility between signals and expressions
is structural and never inferred from rendered bracket text. Indexing a
non-scalar shape selects its leading unpacked dimension and preserves the data
type; only after the shape is scalar does indexing select from the packed data
type. Unpacked array slicing and concatenation are not accepted until their
semantics have an explicit structural representation.

The declared entity is a **signal**. Thus `sample` above is a signal whose data
type is `int[16]` and whose transfer type is `buf`. Ordinary SystemVerilog nets
and variables are signals too: their declarations imply the appropriate static
transfer type. `net` retains its precise SystemVerilog realization meaning and
is not a second Pigen umbrella term. Pigen declarations are parsed into
separate data-type, transfer-type, and declarator-shape objects; their meaning
is never recovered by rewriting or reparsing emitted ranges.

### Module input endpoints

A Pigen module input declares a data type and may optionally constrain its
transfer type:

```systemverilog
input int[16] sample;
input int[16] buf queued_sample;
output int[16] buf result;
```

Every input is a signal. Payload and valid enter the module, ready leaves it,
and the module consumes a value only on their handshake. An unqualified input
has an abstract transfer type: a type variable constrained by the universal
ready/valid boundary contract and specialized by its connection context. An
explicit transfer type constrains that variable; connecting an incompatible
signal is an error. The annotation does not otherwise change the receiving
body's consumption model. The body need not know whether the producer is a
wire, register, port, buffer, FIFO, or skid.

There is no Pigen signal without formally defined valid and ready behavior. A
connection to a static may allow constant laws to lower to `1'b0` or `1'b1`
and unused control paths to be optimized away. This is a lowering consequence,
not an erasure of the signal or its transfer type from the semantic model. A
module can therefore stimulate or stall external dataflow through ready while
remaining parametric over its peer's realization.

The unqualified-output contract has not yet been chosen. Until it is specified,
Pigen outputs require an explicit transfer type. Their consumer is likewise
unknown to the sending module; the output exposes payload, valid, and ready
according to the selected local realization.

For internal signals and explicitly realized outputs, the transfer laws
are:

| Transfer type | valid / ready | Semantics |
| --- | --- | --- |
| `wire` | `1` / `0` | Always offered combinational value; not a signal destination. |
| `reg` | `1` / `1` | Persistent, always-available value; reads do not consume. |
| `logic` | `1` / `1` | The SystemVerilog variable form of the `reg` static law. |
| `buf` | occupancy / elastic readiness | One-entry elastic storage. |
| `port` | one-cycle pulse / `1` | A directly written payload register with a one-cycle Pigen-valid pulse. |
| `fifo` | nonempty / nonfull | Ordered depth-N storage and a combinational ready-chain break. |
| `skid` | nonempty / nonfull | Exact two-entry skid buffer with registered backpressure. |

Generated RTL uses constants rather than private valid/ready state for statics
where the connection context permits.

`wire`, `reg`, and `logic` are always valid. `validate(x)` on one of these
types is accepted as a no-op and produces a compiler warning; no validity
hardware is emitted. `invalidate(x)` is an error because an always-valid
signal cannot be made invalid. `flush(x)` is also invalid because these
types have no signal occupancy to empty.

An output `port` is set-and-forget: it does not hold `valid` while downstream
`ready` is low. If its one-cycle valid pulse ends without a cycle in which
`ready` is high, that token was not transferred and is lost. Use `port` only on
direct or otherwise predictably low-contention paths where this is acceptable.
When delivery must survive backpressure, the user must select an output `buf`,
`skid`, or `fifo` with sufficient capacity. Fabric topology optimization may
prefer low-contention routes for `port` outputs, but such a preference cannot
replace storage and is not a delivery guarantee.

## Transfers and signal actions

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

A transfer is atomic. It fires only when its structured control
path is enabled, its destination is ready, and every *distinct* signal RHS
operand is valid.  Buffered RHS operands are consumed together; their ready
routes include all other buffered operand validities, so a join never partially
consumes.  Repeated use of one operand consumes it once.

The LHS may be one complete buffered signal destination, a static lvalue, or a
concatenation mixing those forms. Buffered destinations must be written whole
because their valid bit describes the whole packet. Static `reg`/`logic`
signals may use normal SV slices,
members, array elements, indexed part-selects, and nested lvalue
concatenations. The RHS is one ordinary SystemVerilog value expression and may
itself be a concatenation. Assignment uses the ordinary
concatenation bit stream: the leftmost destination receives the most
significant portion and the rightmost destination receives the least
significant portion.  Only the aggregate widths must match; top-level item
counts and individual item widths need not match.  Thus all of these are the
same form of atomic transfer:

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

Any signal read needed to evaluate a member participates in the block's
validity and ownership calculation, including a signal projection used in
an lvalue address or select. Thus `state_mem[handle]` requires `handle` to be
valid. If `handle`, `result`, and `next_state` are projections of one packet,
the block is one deduplicated consumer of that complete packet, not three
consumers. Constants and statics retain their normal rules.

`transfer` is contextual: it introduces this extension only when followed by
the complete `transfer begin ... end` form in a clocked procedural block.
Otherwise it remains an ordinary SystemVerilog identifier.

The complete statement has one destination set and one consuming source set.
It fires when every destination is ready and every distinct buffered base
signal read anywhere in the RHS is valid.  Constants add no validity or
consumption dependency: as sources they behave like always-valid wires and
have no readiness state.

A select or member access projects bits from a signal payload; it does not
create a smaller signal. An unpeeked read such as `x <= a[7:0]` therefore
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
transfer fires. A purely static statement containing no buffered signal is
ordinary SystemVerilog and retains normal procedural semantics, including
source-ordered nonblocking assignments to the same variable. The
single-consumer and producer-exclusivity rules apply only where a transfer
type's law requires them. A procedural `wire` destination is rejected,
consistently with normal SystemVerilog variable-assignment rules.

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
signal identifiers. `valid` and `ready` also require one signal
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

An ordinary sequential storage write can consume a signal source directly:

```systemverilog
input uint[8] port data_in;
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
signal identifiers.

`validate(x)` and `invalidate(x)` are synchronous next-state writes to a local
stored signal's valid bit. They force `x` valid or invalid, respectively,
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
forces its output invalid. `flush(x)` empties all buffered contents. These
actions apply only to locally owned `buf`, `fifo`, `skid`, and `port` signals.
Applying a validity action to a module input is an error because the producer,
not the receiver, owns input validity. On an always-valid `wire`, `reg`, or
`logic`, `validate` warns and does nothing, while `invalidate` and `flush` are
errors.

Buffered signals have one consumer. Multiple writes to one destination are
allowed only when semantic control analysis proves their paths mutually
exclusive; all other fanout or producer ambiguity is an error.

A consuming transfer group may not read one of its own buffered destinations.
For example, `x <= x + 1;` is an error: lowering it as an ordinary signal
transfer would connect `x`'s output-ready route to its own input-ready route.
The same rule applies when the self-consumption crosses members of a co-sliced
group. Feedback must instead pass through a distinct element that breaks the
combinational ready dependency, such as ordinary register state or a storage
stage specifically implemented as a ready-chain break. More generally, a
combinational ready-dependency cycle is invalid even when it spans several
connections.

A co-sliced assignment is one consuming transfer group. Every buffered LHS
item is a distinct, complete signal destination; static lvalue items may be
projected. The RHS is flattened and repartitioned by LHS
widths using ordinary SystemVerilog concatenation order;
the two sides need equal aggregate width, not equal arity. The group fires only
when every destination is ready and every distinct non-`peek` base signal
across the RHS is valid. All members update together and each buffered source
is consumed once. A grouped conditional transfer,
`if ({a, b} <= expression)`, tests that same all-member acceptance event.

`peek(x)` takes exactly one signal identifier and lowers to its payload
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

The event control defines a domain. A signal binds to the domain of
its first Pigen use, and all later uses must match. A conventional top-level
`if (reset) ... else ...` branch is preserved and naturally gates actions to
the non-reset path, but it is optional. When a module declares `reset`,
generated storage connects to it; otherwise generated storage uses an inactive
reset. Unbound storage, cross-domain signal use, multiple event edges, and
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
signal transfer; use `accepts` when it must.

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
