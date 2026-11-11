---
status: green
revised_at: "2026-11-11T19:57:35+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Current pigen_syntax_kind includes compilation unit, module, parameter, typedef, unified signal declaration/declarator, clocked process, procedural block, if, when, nonblocking assignment, pipeline, stage, fsm, fsm state, goto, instance, fabric, fabric connection, connection, action, retained and opaque. Nodes retain location and parent/child/sibling IDs; the tree borrows expanded source and owns nodes/shape/expression/type arenas. A PIGEN_SYNTAX_PIPELINE node carries its name token and a single final yield expression; a PIGEN_SYNTAX_STAGE node carries an optional name token (has_name) and its items as children. A PIGEN_SYNTAX_WHEN_STATEMENT carries its condition expression and one body child. A PIGEN_SYNTAX_FSM carries an optional name token (has_name) and its clock expression, with ordered level-zero statement children and PIGEN_SYNTAX_FSM_STATE children; each state carries its name token and one body statement, and PIGEN_SYNTAX_GOTO carries its target token.
