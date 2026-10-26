---
status: green
revised_at: "2026-10-27T01:20:09+11:00"
---

The FSM step, decomposed in-order (per what/is/the/plan.md and what/are/the/remaining/architecture/migration/gates.md): (s1) the structured FSM syntax node — COMPLETE; (s2) the FSM identity owner; (s3) the parent-module lowering skeleton; (s4) the discriminating manifest; (s5) the implementation to that manifest; then integration/landing. The current frontier is (s2).

Owners: what/is/an/fsm.md (an FSM owns a state type, one initial state, unique state identities and resolved transition targets; it lowers to one synchronous controller reset to its header initial state; state actions use shared signal/expression/guard/domain/ownership services, not private resolvers); how/to/write/fsm/states.md (grammar `state identifier : statement`, one statement or ordinary begin/end, actions run only while the state is active, goto is terminal on its syntactic path and changes state only under its explicit enclosing guard, overlapping transitions require structural exclusion); does/goto/wait/for/a/transfer.md (goto never implicitly waits for a transfer); what/is/the/spec.md (an FSM is a synchronous FSM).

(s1) the structured FSM syntax node — COMPLETE. include/pigen/syntax.h carries PIGEN_SYNTAX_FSM (as.fsm: name, clock and reset as expression ids, initial token), PIGEN_SYNTAX_FSM_STATE (as.fsm_state.name) and PIGEN_SYNTAX_GOTO (as.goto_statement.target). src/syntax.c parse_fsm parses one module-level `fsm NAME @(posedge clk) reset (name) initial INIT begin state ID: statement ... end` item into structured nodes with original spans, dispatched from parse_module_items; state bodies use the structured statement forms plus `goto IDENTIFIER`, which parses only inside an FSM body. The event control is exactly one posedge clock edge, reset is one signal name, state names are unique, and the initial state and every goto target must be declared states (a goto may forward-reference a later state). A failed parse reports the diagnostic at the original span and returns 0. The quarantined textual prototype's fsm.c/fsm.h (pigen_lower_fsms) is a different owner, untouched; its deletion waits for the later coverage stage.

(s2) the FSM identity owner (include/pigen/fsm_identity.h + src/fsm_identity.c): the owner seam that establishes the FSM's module-scope state type, its one initial state, its unique state identities and its resolved transition targets in the resolved semantic model, as the pipeline's step-(a) owner establishes a pipeline's outgoing-packet identity. A failed call returns -1 and the compile stops; it may leave partial state (how/should/compiler/builders/fail.md). Target fsm-identity-test.

(s3) the FSM parent-module lowering skeleton (include/pigen/fsm_lower.h + src/fsm_lower.c): the owner seam that lowers one PIGEN_SYNTAX_FSM statement through the common RTL model into the parent module, consuming the (s1) node and the (s2) established state/initial/transition identities and publishing one synchronous controller as common RTL objects/equations/processes: the state register reset to the header initial state, the per-state action bodies (ordinary statements through the shared services), and the guard-gated goto transitions with structural exclusion of overlapping transitions. A failed call validates the arguments, reports the diagnostic at the original span and returns -1, and may leave partial state (how/should/compiler/builders/fail.md). SKELETON: the entry validates and returns -1 without publishing RTL, until (s5).

(s4) the discriminating green manifest (tests/fsm_lower_test.c, target fsm-lower-test): the binding (s5) contract, its sections red against the (s3) stub and green once (s5) lands; the seam reds (a non-FSM node id the lowering must reject) hold throughout.

(s5) the implementation to that manifest, then integration/landing.

After (s5): child-instance and fabric, in that order, each in its own bounded stage with a discriminating green test manifest.
