---
status: green
revised_at: "2026-11-11T16:04:09+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects an omitted producer output to a sibling input without intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wiring remains supported. Output transfer types remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains an abstract handshake input.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward sibling references and later module definitions resolve. Omitted ports receive typed parent signals whose instance syntax identity distinguishes them from public module ports, including through nested instantiation. RTL objects remain internal; boundary views introduce no storage. Member analysis resolves instance.port to these semantic endpoints.

An elastic child output wired to a static parent payload has a typed endpoint_view. Its payload and explicit symbolic valid/ready bindings map to the actual parent RTL objects. Each omitted control reuses a declared `payload_valid` or `payload_ready` signal when present; otherwise lowering creates an internal control net. Child instance emission and downstream dot access use the same objects. Payload-only, valid-only and ready-only explicit bindings therefore preserve backpressure. Structured emission owns handshake connections; specialization no longer inserts guessed control names.

Parent transfer offers exclude their own child input readiness to avoid circular valid/ready dependency; consumption still uses fire. Child consumer ownership recognizes both a typed output view and references to its explicitly connected payload wire. Duplicate child consumers, including mixed dot/wire connections, and child/local consumption conflicts are rejected against the same view identity. Fabric ownership and broader aliases still require unification.

`make child-port-endpoint-test` simulates sibling wiring, parent reads/writes, routing, forward references, module ordering, nested instantiation, full/partial explicit output bindings and valid/ready/peek under backpressure. Negative fixtures cover writes to child outputs, missing ports and duplicate consumers. The combined feedback syntax example elaborates. The positional child regression covers conventional declared handshake names.

Remaining: general endpoint expressions, input-to-input aliases, arrays/generate scopes, non-symbol control bindings, broader alias/fabric ownership and unused-endpoint defaults. These are incomplete implementation areas, not restrictions on parent-wide endpoint access.
