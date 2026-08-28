# README maintenance

`README.md` is the short public introduction to Pigen. Explain why the project
exists and make the transfer model legible to a hardware designer; do not turn
it into a compiler architecture or development diary.

Front-load transfer types. They are Pigen's defining feature: ordinary
SystemVerilog signals and elastic storage share one ready/valid transfer
interface, and `<=` performs that transfer. Examples should make the removal of
handshake bookkeeping concrete.

The type-system introduction must nevertheless state the full product once:

```text
signal = data type × transfer type × declarator shape
```

Keep the data-type section brief. The current Pigen primitives are `int[n]`,
`uint[n]`, and `bit`; use `bit[8]` for neutral eight-bit examples. Ordinary
SystemVerilog `byte` is a distinct signed type, never a Pigen alias. Use
`int[16] fifo[8] pending[lanes];` when depth/shape separation needs to be
explicit.

Leave canonical identities, alias records, expression resolution, and RTL IR
to `SPEC.md`, `PLAN.md`, and `agent_notes/`. The public README should explain
abstract unqualified inputs and the need to realize internal/output signals
where data-type policy requires it, but not narrate the implementation APIs.

Keep the tone direct, technical, and unpromotional. Historical architecture
status belongs in approved records; current status belongs in `PLAN.md`. Until
the vertical RTL slice connects the structured frontend to `./pigen`, clearly
label data-first snippets as target language and point runnable production
users to `USER_GUIDE.md`. Never show transfer-first syntax as the public Pigen
language.

Prefer the concise state form in FSM examples. If a state needs multiple
statements, write `state name: begin` on one line.
