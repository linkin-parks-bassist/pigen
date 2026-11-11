---
status: green
revised_at: "2026-11-11T16:06:42+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects an omitted producer output to a sibling input without intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wiring remains supported. Output transfer types remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains an abstract handshake input.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward references and later module definitions resolve. Omitted ports receive typed parent signals distinguished from public module ports by instance syntax identity, including through nested instantiation. RTL boundary views remain internal and introduce no storage.

An elastic child output wired to a static parent payload has a typed endpoint_view. Its payload and explicit symbolic valid/ready bindings map to parent RTL objects. Each omitted control reuses a declared `payload_valid` or `payload_ready` signal when present; otherwise lowering creates an internal net. Child instance emission and downstream dot access share these objects. Structured emission owns handshake connections; specialization does not insert guessed control names.

Computed elastic inputs such as `.i(a.o + 8'd1)` become storage-free structural transfers into synthetic child boundary signals. They reuse typed expression, acceptance and ownership analysis, without inventing a clock. Arithmetic over two child outputs joins their tokens atomically; explicit output aliases work as operands. Conditional expressions, explicit control overrides on computed inputs and wider expression forms still need auditing. See what/is/a/clock/domain.md for structural process representation and the unaudited cross-instance clock boundary.

Parent transfer offers exclude their own destination readiness to avoid circular valid/ready dependency; consumption uses fire. Child ownership recognizes typed output views and their explicit payload wires. Duplicate child consumers, mixed dot/wire consumers, and child/local conflicts are rejected. Computed connections participate through normal transfer ownership. Fabric ownership and broader aliases remain incomplete.

`make child-port-endpoint-test` simulates direct and arithmetic sibling wiring, a two-source join with a slower producer, parent reads/writes, routing, forward references, module ordering, nesting, full/partial output bindings and valid/ready/peek under backpressure. Negative fixtures cover output writes, missing ports and duplicate consumers, including an expression plus a direct consumer. The combined feedback example elaborates; the positional child regression covers conventional handshake names.

Remaining: wider endpoint expressions, input aliases, arrays/generate scopes, non-symbol controls, broader alias/fabric ownership and unused-endpoint defaults. These are implementation gaps, not restrictions on parent-wide endpoint access.
