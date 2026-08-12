# Inline Pipeline Specification and Refactor Plan

Status: implementation design following the August 2026 pipeline decision.

This document replaces the current model in which a top-level `pipeline`
block emits a public pipeline module, one module per stage, and a private skid
module. The intended model is instead:

- a pipeline is written lexically inside a clocked `always` or `always_ff`
  block in an ordinary
  module;
- it declares persistent hardware in the enclosing module rather than being a
  statement which conditionally executes;
- all pipeline registers, ready/valid logic, and skids are elaborated into that
  module; and
- fabrics remain autonomous generated modules.

The placement is procedural because it is the natural place to state the
pipeline's clock domain and reset policy. Its meaning is elaborative because a
pipeline is persistent hardware, not an operation performed when program
control reaches its declaration.

## Compatibility requirement

Pigen is an extension of SystemVerilog. An ordinary SystemVerilog source file
which does not use a Pigen extension must retain the same behavior after being
passed through Pigen. This is an overarching language and release requirement,
not merely a parser convenience.

In particular:

- `pipeline`, `stage`, and `pipe_reset` remain usable as ordinary identifiers
  outside the complete contextual Pigen forms defined here.
- A call such as `pipe_reset(something);` is ordinary SystemVerilog unless its
  argument resolves to an inline Pigen pipeline in the permitted scope.
- Pigen must not reserve a new global keyword or reinterpret an unrelated task
  call.
- Generated names must be checked for collisions with user declarations. A
  collision produces a useful diagnostic; it must never silently change the
  meaning of the input program.
- The compatibility suite must compile and simulate representative `.sv`
  inputs both directly and after Pigen, and compare their externally visible
  behavior.

Compatibility with ordinary SystemVerilog does not require indefinite support
for the old, Pigen-specific top-level pipeline syntax. That syntax should get a
specific migration diagnostic once inline pipelines replace it.

## Proposed surface syntax

Pipeline syntax is token-based and Verilog-like. The tokenizer throws away
whitespace and comments; indentation and line breaks never affect the grammar.

The inline form has packed input and output expressions in its header. The
final yielded names are hoisted pipeline-output projections which can be used
by later ordinary transport statements in the enclosing module:

```systemverilog
module mac #(parameter int W = 16) (...);
    port [W-1:0] m_in, x_in, b_in;
    buf  [W-1:0] result_out;

    always @(posedge clk) begin
        if (reset)
            pipe_reset(mac_pipe);
        else if (enable) begin
            pipeline mac_pipe {m_in, x_in, b_in} yields {result} begin
                logic [W-1:0] m, x, b, result;
                logic [2*W-1:0] mx;

                stage {
                    m, x, b
                } yields {
                    m * x,
                    b
                };

                stage {
                    mx,
                    logic [W-1:0] b_next
                } yields {
                    logic [W-1:0] result
                } begin
                    result = (mx + b_next)[W-1:0];
                end
            endpipeline

            result_out <= result;
        end
    end
endmodule
```

The corresponding grammar is:

```text
pipeline-declaration ::= "pipeline" identifier packed-expression
                         "yields" packed-expression
                         "begin"
                           pipeline-declaration-item*
                           pipeline-option* stage+
                         "endpipeline"

pipeline-declaration-item ::= packed-type identifier-list ";"
stage                ::= "stage" identifier? packed-stage-list "yields"
                         packed-stage-list (";" | "begin" stage-body "end")
packed-stage-list    ::= "{" stage-item ("," stage-item)* "}"
stage-item           ::= expression | packed-type identifier
pipeline-reset       ::= "pipe_reset" "(" identifier ")" ";"
```

The braced inputs form one packed, atomic transport group in lexical order.
The first stage repartitions that group by total packed width. The final
yielded packing similarly forms one packed output packet; each yielded name is a
projection of that packet, not an independently buffered value. Reading
several yielded names in one grouped transfer is therefore one consumer of the
complete output packet.

Every intermediate boundary follows the same rule: member count and individual
member widths may change freely, provided the total packed widths match. Pigen
emits an elaboration-time `$bits` check. A yielded expression can inherit its
type from a same-position next-stage item only across equal arity; when a stage
repartitions, give the computed value an explicit typed name first.

Pipeline-local declarations establish the types of names once for the entire
pipeline. A bare name at any stage boundary uses that established type. The
same type association may instead be introduced or restated inline in either
curly-bracket packing list for convenience:

```systemverilog
stage {handle_t handle, sample_t sample}
    yields {handle, acc_t acc};
```

Both forms populate one pipeline-local type environment. Repeating a type for
an existing name must be compatible; it is a checked restatement, not a new
shadowing declaration. A name introduced inline is available to later stages.
Curly braces retain packing semantics throughout—they do not declare tuple or
record types.

A yielded expression need not acquire a throwaway name. Its packed position
and width are consumed by the corresponding declared name at the following
stage boundary. A stage consisting entirely of packed expressions ends with
`;`; a stage needing combinational statements uses `begin ... end`. Stages may
be anonymous. Optional stage names may be retained as labels for diagnostics
and generated comments, but are not required for semantics.

The initial stage-body model is deliberately combinational. A possible future
extension may allow transport operations inside a stage body; such an operation
would stall that stage until its nested transfer fires. An explicit `stall();`
directive is also reserved as a future way to suppress a stage advance. Neither
form is accepted in the first inline-pipeline implementation, so there is no
implicit hidden busy mechanism today.

The existing `skid;`, `no_skid;`, and `skid_step` controls remain available.
Pipeline-local module parameters do not: an inline pipeline uses
parameters and localparams from its enclosing module. This avoids creating a
second parameter namespace whose values cannot naturally be overridden once
there is no generated module instance.

The exported final names are read-only pipeline outputs. They are hoisted with
the pipeline object, so later statements may use them regardless of where the
declaration appears textually. A name collision with an enclosing declaration
is an error rather than implicit capture or shadowing.

## Elaboration, scope, and control flow

A pipeline declaration is hoisted to the enclosing clocked procedural scope.
Its storage has the lifetime of the enclosing module. The declaration:

- may be referenced by `pipe_reset` before its textual occurrence;
- must have a name unique among pipelines in its enclosing module;
- belongs to exactly one clocked `always`/`always_ff` block and therefore one clock
  domain; and
- may not be reset or otherwise controlled from another clocked block.

The complete procedural reachability guard around the declaration is the
pipeline-wide enable. The declaration is still hoisted for storage and scope;
the branch controls whether that persistent hardware may transfer on a clock
edge. For example, the declaration beneath `else if (enable)` above elaborates
with reset priority followed by an `enable` branch.

The inferred enable applies to the pipeline as a whole, including every stage,
skid, input endpoint, and output endpoint. While disabled:

- all stage and skid state is held;
- the input group reports not-ready, so no source is consumed; and
- the exported output packet reports not-valid, so no consumer can accept or
  repeatedly observe a stalled packet as a new transfer.

Stored validity is retained internally and becomes visible again when the
pipeline is re-enabled. An unconditional declaration has an effective enable
of `1'b1`. Nested `if`/`else`, `case`, and equivalent supported procedural
structure contribute their normal complete guard, using the same guard analysis
as ordinary transport assignments.

Both `always` and `always_ff` are accepted in source, with plain `always` the
preferred spelling. The first implementation supports the same domain
restriction as transport actions: exactly one positive-edge event in the
enclosing block. Support for asynchronous event controls is deferred until
asynchronous reset semantics are deliberately specified.

## Reset semantics

`pipe_reset(pipe);` and `pipeline_reset(pipe);` are declarative reset bindings,
not task invocations or registered requests to reset on the following cycle.

For example:

```systemverilog
always @(posedge clk) begin
    if (reset || flush)
        pipe_reset(pipe);
    else begin
        pipeline pipe {input_packet} yields {output_packet} begin
            // stages
        endpipeline
    end
end
```

causes the generated pipeline storage to use `reset || flush` directly in the
highest-priority branch of its generated sequential logic:

```systemverilog
always_ff @(posedge clk) begin
    if (reset || flush) begin
        // clear every generated valid bit and skid occupancy bit now
    end else begin
        // ordinary elastic transfers
    end
end
```

Consequently there is no extra cycle of reset latency.

The reset condition is the complete procedural guard at the `pipe_reset` call.
Multiple calls naming the same pipeline in the same clocked block are combined
with logical OR. A bare, unconditional call means the pipeline is held in
reset. Without an explicit binding, Pigen uses a parent module port named
`reset` as a conventional synchronous reset when present; without that port,
the pipeline emits unreset storage and its initial simulation validity may be
unknown.

Reset clears all architecturally meaningful validity and occupancy state.
Generated payload registers may also be assigned `'0` for deterministic RTL,
but payload values while invalid have no language-level meaning.

`areset`, `pipe_areset`, and similar asynchronous forms are reserved. They must
not be implemented merely by recognizing a differently named call; the event
control, assertion behavior, deassertion behavior, and target technology all
need a coherent rule first.

## Endpoint and transfer semantics

The braced input packing and exported final yields use the same
transport-expression rules as an ordinary grouped transfer.

- The total packed width of the input group must equal the total packed width
  of the first stage's input packing.
- Concatenations, slices, constants, and mixed member widths are allowed.
  Packing follows normal SystemVerilog concatenation order.
- Constants in the input group are always valid and are never consumers.
- All non-degenerate input transports are consumed atomically when the first
  stage accepts the complete input packet.
- The final packet transfers when an ordinary downstream statement consumes
  one or more of its yielded projections and every destination in that grouped
  statement is ready.
- If there is no active consumer for the final packet, it is not ready and the
  pipeline eventually stalls rather than discarding results.
- Reading one or more slices of the same transport within this single endpoint
  expression is one consumer and consumes the transport's complete packet.
- The existing mutually-exclusive-branch rule applies. A non-degenerate
  transport may have at most one active consumer, not merely one textual
  mention.
- Degenerate `reg`, `logic`, and `wire` cases use the ordinary transport rules;
  pipelines do not create a second interpretation for them.

The explicit atomic-transfer block is the preferred readable spelling when a
pipeline result drives unlike destinations:

```systemverilog
transfer begin
    bqd_out <= acc_out;
    state_mem[handle_out] <= state_out;
end
```

This is exactly `{bqd_out, state_mem[handle_out]} <= {acc_out, state_out};` in
source order. It has one fire event and is one consumer of the complete final
pipeline packet. Reads used to compute an lvalue, such as `handle_out` in the
memory index, contribute validity but are deduplicated with `acc_out` and
`state_out` when they project the same packet.

Adjacent stage boundaries should be checked by total packed width rather than
requiring equal list arity and equal width member-by-member. This makes stage
packing consistent with ordinary slicing and co-slicing:

```systemverilog
stage {logic [15:0] packet}
    yields {logic [7:0] upper, logic [7:0] lower} begin
    {upper, lower} = packet;
end
```

Internally, every stage boundary remains one atomic ready/valid packet. A skid
buffer, when requested, buffers that entire packed packet.

## Generated SystemVerilog

No module is emitted for the pipeline, its stages, or its skid buffers. Pigen
inserts the following immediately before the enclosing module's `endmodule`,
grouped under a comment naming the source pipeline:

- packed stage payload signals;
- stage valid and ready signals;
- first- and last-endpoint handshake wiring;
- optional inline skid storage and control;
- combinational stage transforms; and
- sequential stage and skid state updates using the enclosing clock and the
  inferred reset expression.

The source pipeline declaration and `pipe_reset` pseudo-calls are replaced by
syntactically harmless empty statements while preserving line structure for
diagnostics. Generated declarations and processes are module items; they are
not literally nested inside the source clocked block.

One generated `always_ff` per pipeline is preferred initially. It makes reset
priority and shared transfer decisions visible in one place. Separate
`always_comb` blocks per stage are acceptable when they improve name isolation
and diagnostics. The important constraint is that the registers live in the
parent module and have no hidden port, instance, or namespace boundary.

The old public `clk`, `reset`, `enable`, `in_valid`, `in_ready`, `out_valid`,
`out_ready`, `packet_in`, and `packet_out` module interface disappears. Clock
comes from the enclosing clocked block; reset comes from `pipe_reset`; packet
flow comes from the input group and exported final yields; and the declaration's
procedural guard is the replacement for the old pipeline-wide `enable` port.

Fabrics are unchanged by this work. They remain separately generated modules
because they are autonomous connection structures with a meaningful module
boundary. Pipeline endpoints can ultimately connect to fabric-facing ports in
the same way as any other transport.

## Diagnostics

The implementation should diagnose at least:

- a pipeline outside an ordinary module or outside a clocked `always`/`always_ff`;
- duplicate pipeline names in one module;
- `pipe_reset` naming no pipeline, naming an ambiguous object, or crossing a
  clocked-block/clock-domain boundary;
- an unsupported event control;
- missing stages or duplicate stage names;
- input-to-stage or stage-to-stage total width mismatches;
- malformed input expressions or attempts to write an exported pipeline
  projection;
- multiple active consumers, including consumers hidden behind slices;
- generated-name collisions; and
- use of the legacy top-level pipeline form, with an inline migration example.

Diagnostics should point to the source construct, not the generated fragment.

## Refactor plan

### 1. Preserve the current behavior as a reference

- Keep the existing pipeline simulation tests as behavioral references before
  changing their source syntax.
- Add backpressure, simultaneous input/output, forced skid, periodic skid, and
  reset-on-an-active-packet cases where coverage is currently thin.
- Record the packed packet ordering produced by the present stage parser.

### 2. Separate fabric and pipeline lowering

`src/blocks.c` currently combines two increasingly unrelated jobs. Split the
pipeline model/parser/renderer from fabric generation, or at minimum give them
separate discovery and rendering entry points. Keep the top-level design-unit
pass for fabrics.

Replace the current rule which skips every ordinary module wholesale. The new
pipeline discovery pass must be aware of module boundaries, clocked blocks,
boundaries, nested procedural blocks, comments, strings, and source spans.

### 3. Add a module-aware inline pipeline model

Extend the pipeline representation with:

- enclosing module identity and insertion offset;
- declaration and containing-clocked-block spans;
- normalized clock domain;
- input expression spans and exported final-yield symbols;
- the declaration's complete procedural enable guard;
- reset-call spans and their complete guards; and
- stable generated names for every stage and skid boundary.

Retain the existing stage parser, declaration parser, signedness handling,
skid-option parser, and most packed-boundary type resolution. Change adjacency validation
from member-wise equality to total packed-width equality.

### 4. Make discovery cooperate with procedural parsing

Inline pipeline bodies are not ordinary procedural statements, so they must be
discovered and blanked with whitespace before the existing procedural AST pass
walks clocked blocks. Preserve newlines and offsets. Parse the resulting procedural
structure to obtain the declaration's clock domain and each `pipe_reset`
call's guard.

Recognition is contextual and two-phase: collect named pipeline declarations
first, then reinterpret only matching `pipe_reset(name)` calls. Leave all other
calls untouched as ordinary SystemVerilog.

### 5. Bind endpoints through the normal transport model

Do not implement a second ready/valid expression analyzer in the pipeline
renderer. Reuse the existing transport-expression, width-check, grouped-ready,
grouped-valid, consumption, and mutually-exclusive-consumer machinery.

Represent the pipeline input as one grouped consumer and its exported final
packing as one grouped producer with named projections. This is what makes
constants, slices, co-slices, mixed widths, and branch exclusivity behave
exactly like ordinary transfers. Qualify both endpoint handshakes with the
pipeline-wide enable.

This integration is the highest-risk part of the change: the current assignment
model is organized around rewritten semicolon-terminated statements, while a
pipeline endpoint is an elaborated producer/consumer whose handshake persists.
It will likely need a small generalized endpoint or route representation rather
than synthetic assignment text.

### 6. Replace module renderers with an inline renderer

Reuse the current packing and stage combinational-body emission, but replace:

- `render_pipeline_stage` module wrappers;
- `render_pipeline_skid` module wrappers;
- `render_pipeline_top`; and
- all stage/skid instantiation code.

Emit module-scope signals and parent-module processes instead. Generate the
ready chain from the last destination back to the first source, preserving the
existing elastic and skid behavior. Combine reset guards and put them directly
in the generated sequential reset branch.

Store generated fragments per enclosing module and insert each set at that
module's `endmodule`. The current single `block_output` appended at end of file
remains suitable for fabrics only.

### 7. Migrate syntax, tests, examples, and documentation

- Convert pipeline fixtures from standalone DUTs to ordinary modules containing
  inline pipeline declarations.
- Change structural assertions from looking for generated child modules and
  instances to asserting that none exist and that inline state does exist.
- Update the specification, user guide, README summary, and plan.
- Remove the old generated pipeline interface and top-level syntax after the
  inline tests pass.
- Keep fabric tests unchanged except for mixed-unit tests which currently use a
  top-level pipeline.

## Test matrix

The completed feature needs syntax, diagnostics, emitted-RTL, and simulation
coverage for:

1. one pipeline and multiple pipelines in a module;
2. identical pipeline names in different modules and rejection within one;
3. reset reference before textual declaration;
4. reset of a full, stalled pipeline on the asserting edge, proving no extra
   cycle of latency;
5. multiple reset sites and compound/branch-derived reset guards;
6. a pipeline with no reset binding;
7. declarations under `if`, `else if`, nested conditions, and `case`, proving
   the complete lexical guard is the pipeline-wide enable;
8. disabling a nonempty pipeline, proving inputs are not consumed, outputs are
   not transferred repeatedly, and stored packets resume afterward;
9. source and destination backpressure independently and simultaneously;
10. throughput of one packet per cycle with all stages flowing;
11. forced, suppressed, periodic, and disabled skids;
12. concatenated endpoints, unequal member widths, slices, and co-slices;
13. repeated slices of one source in a single group, proving one complete packet
    consumption;
14. `transfer begin ... end` with mixed buffered, output-port, memory, discard,
    and degenerate destinations, including packet projections in lvalue indices;
15. rejected non-exclusive fanout and accepted mutually exclusive consumers;
16. constants and every degenerate transport kind at endpoints;
17. pipeline-local declarations, inline packed-list declarations, compatible
    restatements, and incompatible redeclaration diagnostics;
18. signed stage values and enclosing module parameters/localparams;
19. total-width mismatch diagnostics at every boundary;
20. ordinary identifiers named `pipeline`, `stage`, `transfer`, and
    `pipe_reset`, including
    a real `pipe_reset` task call which must pass through untouched;
21. no emitted pipeline/stage/skid module declarations or instances; and
22. direct-versus-Pigen behavioral equivalence for an ordinary SystemVerilog
    compatibility corpus.

Use both structural checks on emitted text and self-checking simulation. Where
supported in CI, compile the output with both Icarus Verilog and Verilator so
the inline module-item placement is not accidentally tailored to one parser.

## Size and risk

This is a medium-to-large subsystem refactor, but not a compiler rewrite.
Roughly speaking, the stage grammar, type parsing, option parsing, and core
elastic equations survive. The module wrappers, top-level pipeline interface,
discovery strategy, output placement, reset plumbing, and endpoint integration
do not.

The work is likely on the order of a solid focused week, with a second week
possible if generalizing the transport routing model exposes more assumptions
than expected. The code change will probably be several hundred lines of core
compiler work and around one to two thousand lines once migrated tests,
fixtures, and documentation are counted.

The difficult part is not inlining registers. It is making an elaborated object
inside procedural syntax coexist cleanly with source preservation, ordinary-SV
compatibility, the current guard analysis, and the single-consumer handshake
rules. Once those boundaries are represented explicitly, the actual stage and
skid RTL generation is comparatively mechanical.
