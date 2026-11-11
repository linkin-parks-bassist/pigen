---
status: green
revised_at: "2026-11-11T15:52:13+11:00"
---

Child ports are parent endpoints. `producer a(clk, reset); consumer b(.clk(clk), .reset(reset), .i(a.o), .o(result));` connects the omitted producer output to the sibling input without a declared intermediary wire. Explicit parent wires still work. Parent clocked transfers can read `a.o` or write `b.i`; anonymous/named fabrics can route `a.o -> b.i`. Output transfer types remain explicit by user decision, e.g. `output uint[8] buf o`. Use an explicit static reset (`input wire reset`) for broadcast control; unqualified input bit remains an abstract handshake input.

Resolver instance metadata owns omitted-port signals and their copied data type/shape. Member-expression analysis maps instance.port to a resolved parent signal. pigen_signal_is_instance_endpoint distinguishes generated endpoints from public module ports, including when the parent is itself instantiated. Synthesized endpoint signals carry the instance syntax identity and reverse child direction for parent use; RTL objects remain internal and boundary views introduce no extra storage. Instance output text connects payload plus valid/ready siblings using semantic identities. Fabric lowering can recover controls from resolved endpoint records when explicit control connections are absent. Parent transfer offers exclude their own child input readiness to avoid a circular valid/ready dependency; consumption still uses fire.

`make child-port-endpoint-test` simulates sibling wiring, parent reads, parent writes and routed connections under backpressure, checks rejection of writes to child outputs and missing ports, elaborates the combined feedback syntax example, and simulates a parent containing endpoints instantiated inside another module. Full verification passes.

Remaining: forward sibling references are currently rejected because instances resolve in declaration order; module-definition ordering, instance arrays/generate scopes, arbitrary endpoint expressions and control-access coverage need auditing. Consumer ownership across all connection forms and unused-endpoint defaults also need dedicated witnesses. These are incomplete implementation areas, not restrictions on the parent-wide endpoint requirement.
