---
status: green
revised_at: "2026-11-11T16:48:35+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects siblings. Parent transfers can read `a.o` or write `b.i`; named/anonymous fabrics route `a.o -> b.i`. Explicit wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains abstract.

Module declarations precede body resolution. Sibling identities and omitted endpoints precede direct-wire input aliases, output bindings, then general input expressions. Sibling output forward references and later module definitions resolve. Omitted ports use typed parent signals identified by instance syntax. Control bindings do not count as payload bindings; dot lookup matches the payload name. Boundary views add no storage.

Elastic outputs bound to static payloads, or omitted with explicit controls, have typed endpoint_views. Elastic inputs bound to writable static parent signals acquire output-direction boundary views: parent transfers and fabrics drive their payload/valid and observe child ready. Existing elastic upstream producers retain their source identity and write protection. Instance output bindings to direct wire-bound sibling inputs (`.o(b.i)`) share payload and controls in either declaration order. Parenthesized wire aliases also work; projected payload aliases remain unresolved.

Symbolic controls map to parent RTL objects. Selected controls use scalar endpoints plus equations that observe child outputs or drive child inputs, preserving neighboring bits. Omitted controls reuse declared `payload_valid`/`payload_ready` or get internal nets. Instance emission, dot access and fabrics share these objects. Explicit handshakes accept read-only output-ready and input-valid expressions, including constants and boolean expressions, without generating drivers. Automatic consumers conflict with read-only ready; automatic endpoint writers conflict with read-only valid. Ordinary raw-wire payload writes remain legal.

Computed inputs such as `.i(a.o + 8'd1)` become storage-free atomic transfers. Arithmetic joins sources; concatenations deduplicate reads; ternaries consume all referenced sources (how/does/an/atomic/transfer/fire.md). Explicit `.i_ready(target)` feeds actual child readiness back to generated boundaries. Structural processes have no clock; cross-instance clock compatibility remains unaudited.

After wiring, pigen_lower_rtl_endpoint_defaults zeros undriven generated child-input payload/valid and unconsumed child-output ready. Equations, updates, child connections and primitive outputs count as drivers; borrowed controls retain their owner. Parent offers exclude their own destination readiness, avoiding circular handshake dependency.

Ownership recognizes views and payload wires. Duplicate consumers and child/local conflicts diagnose; fabrics additionally validate exclusive elastic ownership across blocks. Static fanout and peek remain legal. Broader aliases and retained ordinary-SV drivers need auditing; what/is/an/inline/fabric.md owns fabric rules.

make child-port-endpoint-test covers omitted/aliased payloads, full/partial/selected controls, parent transfers, fabric routing and introspection under stalls. child-ready-manual-test covers read-only ready. child-valid-manual-test covers constant/computed valid, clocked raw-wire payload writes and conflicting automatic producers.

Remaining: projected input aliases, other expressions, arrays/generate scopes and broader ownership. Whether explicit input-valid overrides gate or replace automatic source validity is unresolved. Specialization and packed counts belong to how/does/specialization/reach/a/fixed/point.md and how/to/normalize/declaration/counts.md.
