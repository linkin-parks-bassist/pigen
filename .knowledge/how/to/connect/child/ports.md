---
status: green
revised_at: "2026-11-11T16:29:14+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects siblings without intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains abstract.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward references and later module definitions resolve. Omitted ports have typed parent signals distinguished from public ports by instance syntax identity, including through nesting. Internal boundary views introduce no storage.

An elastic child output wired to a static parent payload has a typed endpoint_view. Payload and explicit symbolic controls map to parent RTL objects. Each omitted control reuses declared `payload_valid` or `payload_ready`; otherwise lowering creates an internal net. Instance emission, dot access and fabric links/merge sources share these objects.

Computed inputs such as `.i(a.o + 8'd1)` become storage-free atomic transfers into child boundaries. Arithmetic joins sources; concatenations deduplicate base reads. Ternaries select payload but consume every referenced source, per how/does/an/atomic/transfer/fire.md. Explicit aliases work as operands. Structural processes introduce no clock; cross-instance clock compatibility remains unaudited.

After transfers and fabrics, pigen_lower_rtl_endpoint_defaults ties undriven generated child-input payload/valid and unconsumed child-output ready to zero. It recognizes equations, updates, child connections and primitive outputs. Borrowed explicit controls retain their owner.

Specialization separates list arguments only at commas outside parentheses, brackets and braces. Nested concatenations survive; replacement arrays grow consistently across plain/parameterized rewrites. The parameter-concatenation regression passes AddressSanitizer.

Parent offers exclude their own destination readiness, avoiding circular handshake dependency. Ownership recognizes typed views and explicit payload wires, rejecting duplicate consumers and child/local conflicts. Fabric validation also rejects conflicting child/local consumers or producers and wrong-direction/missing endpoints. Peek remains non-consuming beside a fabric route. Fabric groups span parent blocks; broader aliases and retained ordinary-SV drivers remain unaudited; see what/is/an/inline/fabric.md.

`make child-port-endpoint-test` covers expression and parameterized connections, joins at different rates, parent transfers, routing, forward references, nesting, partial bindings, introspection and unused defaults under backpressure. Negative fixtures cover output writes, missing ports and duplicate consumers; fabric-mixed-ownership-test covers mixed connection conflicts. fabric-implicit-merge-test checks lossless two/three-source routing through omitted ports and payload aliases under stalls. Positional tests cover conventional handshake names.

Remaining: explicit control overrides on computed inputs, other expressions, input aliases, arrays/generate scopes, non-symbol controls and broader ownership coverage. Packed declaration-count rules are in how/to/normalize/declaration/counts.md.
