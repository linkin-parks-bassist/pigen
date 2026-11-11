---
status: green
revised_at: "2026-11-11T16:01:48+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects an omitted producer output to a sibling input without declared intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wiring remains supported. Output transfer types remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains an abstract handshake input.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward sibling references and later module definitions resolve. Omitted ports receive typed parent signals whose instance syntax identity distinguishes them from public module ports, including through nested instantiation. RTL objects remain internal; boundary views introduce no storage. Member analysis resolves instance.port to these semantic endpoints.

An elastic child output connected to static parent nets has a typed endpoint_view in its connection metadata. Dot access preserves elastic consumption and direction. RTL maps the view to the actual payload/valid/ready objects; instance emission uses their names. Thus `.o(wire_data), .o_valid(wire_valid), .o_ready(wire_ready)` remains usable as `a.o` in sibling connections, parent transfers and valid/ready/peek observations. Missing or non-symbol control bindings and views over other connection forms still need auditing.

Parent transfer offers exclude their own child input readiness to avoid circular valid/ready dependency; consumption still uses fire. Child consumer ownership recognizes both a typed output view and references to its explicitly connected payload wire. Two child consumers, including one using dot access and one using the raw wire triplet, are rejected. Child/local consumption conflicts are checked against the same view identity. This does not yet unify fabric ownership or every alias expression.

`make child-port-endpoint-test` covers sibling wiring, parent reads/writes, routing, forward references, module ordering, nested instantiation, explicit output control aliases and valid/ready/peek under backpressure. Negative fixtures cover writes to child outputs, missing ports, duplicate consumers and mixed wire/view child consumers. The combined feedback syntax example elaborates.

Remaining: general endpoint expressions, input-to-input aliases, arrays/generate scopes, additional control bindings, broader alias/fabric ownership and unused-endpoint defaults. These are incomplete implementation areas, not restrictions on parent-wide endpoint access.
