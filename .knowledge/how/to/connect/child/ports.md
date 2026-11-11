---
status: green
revised_at: "2026-11-11T16:09:32+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects an omitted producer output to a sibling input without intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains abstract.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward references and later module definitions resolve. Omitted ports have typed parent signals distinguished from public module ports by instance syntax identity, including through nesting. Their internal RTL boundary views introduce no storage.

An elastic child output wired to a static parent payload has a typed endpoint_view. Payload and explicit symbolic valid/ready bindings map to parent RTL objects. Each omitted control reuses declared `payload_valid` or `payload_ready` when present; otherwise lowering creates an internal net. Instance emission and dot access share these objects. Structured emission owns handshake wiring.

Computed inputs such as `.i(a.o + 8'd1)` become storage-free atomic transfers into synthetic child boundaries. Arithmetic joins sources; concatenations deduplicate repeated base reads. Ternaries select payload but consume every referenced source, following how/does/an/atomic/transfer/fire.md. Explicit aliases work as operands. Structural processes introduce no clock; cross-instance clock compatibility remains unaudited.

Specialization separates instance arguments, parameter overrides/defaults and port declarations only at commas outside parentheses, brackets and braces. This preserves nested concatenations. Replacement arrays grow consistently when plain and parameterized instance rewrites mix; the parameter-concatenation regression also passes AddressSanitizer.

Parent offers exclude their own destination readiness, avoiding circular valid/ready dependency; consumption uses fire. Ownership recognizes typed output views and their explicit payload wires. Duplicate consumers, mixed dot/wire consumers and child/local conflicts are rejected. Computed connections use normal transfer ownership. Fabric ownership and broader aliases remain incomplete.

`make child-port-endpoint-test` covers arithmetic, concatenation, conditional and parameterized connections under backpressure, two-source joins at different rates, parent transfers, routing, forward references, nesting, partial output bindings and introspection. Negative fixtures cover output writes, missing ports and duplicate consumers. The feedback example elaborates; positional tests cover conventional handshake names.

Remaining: explicit control overrides on computed inputs, other expression forms, input aliases, arrays/generate scopes, non-symbol controls, broader alias/fabric ownership and unused-endpoint defaults. Packed concatenations used as type counts remain a separate limitation in how/to/normalize/declaration/counts.md.
