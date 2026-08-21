# README maintenance

`README.md` is the short public introduction to Pigen. Its job is to explain
why the project exists and make the central idea legible to a hardware designer,
not to summarize the compiler architecture, specification, plan, or development
process.

Front-load transfer types. They are Pigen's defining feature: ordinary
SystemVerilog signals and elastic storage elements share one ready/valid
transfer interface, and `<=` performs that transfer. Examples should make the
resulting uniformity and removal of handshake bookkeeping concrete.

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
