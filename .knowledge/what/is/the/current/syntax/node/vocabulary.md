---
status: green
revised_at: "2026-10-28T16:51:51+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Current pigen_syntax_kind includes compilation unit, module, parameter, typedef, unified signal declaration/declarator, clocked process, procedural block, if, nonblocking assignment, pipeline, stage, fsm, fsm state, goto, instance, instance connection and opaque. No structured fabric node is present in this enum. Nodes retain location and parent/child/sibling IDs; the tree borrows expanded source and owns nodes/shape/expression/type arenas. Payload-bearing node shapes: a pipeline node carries its name token and a single final yield expression; a stage node carries an optional name token (has_name) and its items as children; an fsm node carries its name token, clock and reset expressions and its initial-state token; a fsm state node carries its name token; a goto statement node carries its target token; an instance node carries its child module type-name token, instance name token and two half-open ranges over its children (first_connection/connection_count over the instance-connection children in source order, first_parameter/parameter_count over the parameter children in source order); an instance connection node carries the connected port name token and the connected expression id.
