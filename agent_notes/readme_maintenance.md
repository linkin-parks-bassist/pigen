# README maintenance

`README.md` is the short public introduction to Pigen. Its job is to explain
why the project exists and make the central idea legible to a hardware designer,
not to summarize the compiler architecture, specification, plan, or development
process.

Front-load transfer types. They are Pigen's defining feature: ordinary
SystemVerilog signals and elastic storage elements share one ready/valid
transfer interface, and `<=` performs that transfer. Examples should make the
resulting uniformity and removal of handshake bookkeeping concrete.

The README's large-scale order should nevertheless reveal that Pigen extends
Verilog's type system before it introduces the higher-level syntax built from
those types. Give data types a short section after the main transfer-type
explanation and before pipelines, fabrics, and FSMs. Roughly four-fifths of the
type-system emphasis belongs to transfer types; data types need only establish
`int[n]`, `uint[n]`, `bit`, `byte`, and the `[n]` width shorthand. Explain a
surface rule once before examples need it rather than interrupting a later
feature section with a backward reference.

Assume readers already know that Verilog has data types. Mention declaration
syntax only as much as examples require, and leave primitive rules, signedness,
packing, arrays, and other exact language semantics to `SPEC.md`. Never expose
compiler terms such as canonical identity, declarator shape, alias record, or
semantic lowering in the opening pitch. Syntactic sugar must not compete with
the transfer-type idea for attention.

Keep the tone direct and technical without becoming exhaustive or promotional.
Architecture status belongs in `PLAN.md` and `agent_notes/`; currently accepted
syntax belongs in `USER_GUIDE.md`. The README may link to those documents rather
than reproducing them. Do not add AI acknowledgements or development-process
copy unless David explicitly wants it there.

Runnable snippets must use syntax accepted by the current compiler unless they
are plainly labelled as intended syntax.

Prefer the concise state form in FSM examples. If an example genuinely needs a
multi-statement state, format its opener as `state name: begin` on one line;
never dangle the state body's `begin` beneath the state label.
