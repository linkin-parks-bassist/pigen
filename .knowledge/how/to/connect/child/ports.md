---
status: green
revised_at: "2026-11-11T17:01:55+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects siblings. Parent transfers read `a.o` or write `b.i`; named/anonymous fabrics route `a.o -> b.i`. Explicit wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains abstract.

Module declarations precede body resolution: sibling identities/omitted endpoints, static signal/slice input aliases, outputs, then general inputs. Forward references and later module definitions resolve. Control bindings do not count as payload bindings; dot lookup matches the payload name.

Typed endpoint_views represent static payload aliases and omitted outputs with explicit controls, without storage. Output slices publish observations while instances retain projections. Input endpoint_view_driven records sibling/transfer/fabric producers: lowering drives the original slice when marked, otherwise observes it. Undriven static projected expressions are continuously valid unless supplied valid qualifies them. Direct aliases retain upstream identities/write protection. `.o(b.i)` supports signal/slice bindings and either declaration order.

Symbolic controls map to parent RTL objects; selected controls use scalar proxies plus observation/drive equations. Omitted controls reuse declared payload_valid/payload_ready or get internal nets. Instance emission, dot access and fabrics share them. Read-only output-ready/input-valid expressions support manual handshakes; conflicting automatic consumers/producers diagnose. Raw-wire payload writes remain legal. Explicit parent-input controls, including scalar and selected signals, are read-only. Resolver ownership and endpoint lowering share pigen_lvalue_is_writable, so these bindings are observed without generated drivers. See how/are/lvalues/represented.md.

Computed inputs such as `.i(a.o + 8'd1)` become storage-free atomic transfers. Arithmetic joins sources; concatenations deduplicate reads; ternaries consume all referenced sources (how/does/an/atomic/transfer/fire.md). Explicit i_ready targets feed actual readiness to generated boundaries. Structural processes have no clock; cross-instance clock compatibility remains unaudited.

pigen_lower_rtl_endpoint_defaults zeros undriven generated input payload/valid and unconsumed output ready. Equations, updates, child connections and primitives count as drivers; borrowed controls retain ownership. Parent offers exclude their own destination readiness.

Ownership recognizes views/payload wires. Duplicate consumers and child/local conflicts diagnose; fabrics enforce elastic exclusivity across blocks. Static fanout/peek remain legal. Child-output producer checks permit provably disjoint constant ranges, indexed selections and indices on static signals in shared coordinates; overlaps and unknown relationships conflict. Buffered signals retain whole-token exclusivity. General projected alias identity, concatenations, local/ordinary-SV drivers and transformed coordinate systems remain incomplete.

make child-slice-ownership-test simulates range/indexed slices under independent stalls, rejects overlaps and disjoint buffered-token producers. child-port-endpoint-test covers payloads, controls, routing/introspection and neighboring bits. child-ready-manual-test and child-valid-manual-test cover manual controls, scalar/selected parent-input controls and conflicting automatic owners.

Remaining: broader projection ownership, other expressions, arrays/generate scopes and clock compatibility. Explicit-valid gate versus replacement policy remains unresolved. Specialization/count owners: how/does/specialization/reach/a/fixed/point.md and how/to/normalize/declaration/counts.md.
