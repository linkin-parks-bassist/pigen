---
status: green
revised_at: "2026-10-29T03:58:24+11:00"
---

When a structured owner resolves one record from a set by token TEXT and returns the FIRST matching record, the manifest's positive witness must discriminate the resolution, not just a success path. Precedents: pigen_instance_identity_establish resolves the type name to the module whose PIGEN_SYNTAX_MODULE name-token text matches (src/instance_identity.c resolve_type_module), and pigen_fsm_identity_establish resolves the header-named initial state to its per-state index (src/fsm_identity.c).

Three rules for the witness:

1. Declare the resolved record NOT first in the arena. For a child-instance, declare the instantiating parent module before the instantiated child module (parent first, child second); for an FSM, the header names a state that is not the first declared state (idle first, run named). A witness that declares the resolved record first lets a resolver that ignores the token text and always returns the first declared record pass undetected, because "first" already equals the resolved record's arena index.
2. Pin the resolved record's identity to its actual (non-first) arena index (the child module's id, the per-state index), not to a property a first-record resolver also happens to satisfy.
3. Prove discrimination with a mutation probe before the packet is accepted: replace the owner's token-text match with a fixed/first-record return and confirm the positive section fails. If the green section passes under the mutation, the witness does not discriminate the resolution and must be reordered (or a second reversed witness added).

The FSM initial-state manifest (tests/fsm_identity_test.c FSM_WITNESS_NONFIRST, header names the second declared state) follows these rules. The accepted (i2) instance-identity manifest (tests/instance_identity_test.c) originally violated rule 1 (child declared first, so an "always first" resolver passed establish-valid-args); its correction re-declares the witness parent-first, child-second and adds a parameterized-instance witness, mirroring the FSM nonfirst pattern. The (i3) instance-lower-test lower-valid-args witness must apply the same parent-first discipline before it lands.
