---
status: green
revised_at: "2026-11-11T16:39:54+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects siblings without intermediary wires. Parent clocked transfers can read `a.o` or write `b.i`; named/anonymous fabrics can route `a.o -> b.i`. Explicit parent wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Use static `input wire reset` for broadcast reset; unqualified input bit remains abstract.

Module declarations precede body resolution; sibling identities and omitted endpoints precede explicit output and input bindings. Forward references and later module definitions resolve. Omitted ports have typed parent signals identified by instance syntax, including through nesting. Control connections do not count as payload bindings; dot lookup matches the payload name rather than shared control metadata. Boundary views introduce no storage.

Elastic child outputs bound to static payloads, or omitted with explicit controls, have typed endpoint_views. Payload and explicit symbolic controls map to parent RTL objects. A non-symbol output-valid binding, such as `.o_valid(flags[1])`, is observed through a scalar RTL equation shared by dot access and fabric routing; both paths have stalled-transfer regressions. Selected output-ready bindings use a scalar ready endpoint and a projected RTL assignment into the selected parent bit, preserving neighboring bits. Each omitted control reuses declared `payload_valid` or `payload_ready`; otherwise lowering creates an internal net. Instance emission, dot access and fabric links/merge sources share these objects. Fully explicit outputs also accept read-only ready expressions, including constants and boolean expressions. Their views observe readiness without generating a driver; automatic sibling, parent-transfer or fabric consumption conflicts with that external handshake owner and diagnoses.

Computed inputs such as `.i(a.o + 8'd1)` become storage-free atomic transfers into child boundaries. Arithmetic joins sources; concatenations deduplicate base reads. Ternaries select payload but consume every referenced source, per how/does/an/atomic/transfer/fire.md. Explicit aliases work as operands. Explicit `.i_ready(target)` bindings feed the generated input boundary's readiness through an RTL equation, so structural transfers observe the ready actually driven by the child. Scalar and indexed ready targets pass the stalled-transfer scoreboard. Structural processes introduce no clock; cross-instance clock compatibility remains unaudited.

After transfers and fabrics, pigen_lower_rtl_endpoint_defaults ties undriven generated child-input payload/valid and unconsumed child-output ready to zero. It recognizes equations, updates, child connections and primitive outputs. Borrowed explicit controls retain their owner.

Specialization preserves nested argument delimiters; see how/does/specialization/reach/a/fixed/point.md.

Parent offers exclude their own destination readiness, avoiding circular handshake dependency. Ownership recognizes typed views and payload wires, rejecting duplicate consumers and child/local conflicts. Fabric groups span parent blocks and validate exclusive elastic ownership; static fanout and peek remain legal. Broader aliases and retained ordinary-SV drivers remain unaudited; what/is/an/inline/fabric.md owns fabric rules and regressions.

`make child-port-endpoint-test` covers omitted payloads with full/partial controls, selected controls, fabric routing and valid/ready/peek under stalls. child-ready-manual-test checks read-only ready wiring and ownership conflicts; fabric checks belong to what/is/an/inline/fabric.md.

Remaining: explicit valid overrides on computed inputs, other expressions, input aliases, arrays/generate scopes and broader ownership coverage. Whether `.i_valid(enable)` gates or replaces automatic source validity is unresolved; confirm that policy before implementing it. Packed declaration-count rules are in how/to/normalize/declaration/counts.md.
