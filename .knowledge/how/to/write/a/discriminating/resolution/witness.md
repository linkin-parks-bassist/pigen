---
status: green
revised_at: "2026-10-31T00:28:16+11:00"
---

When a structured owner resolves one record from a set by token TEXT and returns the FIRST matching record, the manifest's positive witness must discriminate the resolution, not just a success path. Precedents: pigen_instance_identity_establish resolves the type name to the module whose PIGEN_SYNTAX_MODULE name-token text matches (src/instance_identity.c resolve_type_module), and pigen_fsm_identity_establish resolves the header-named initial state to its per-state index (src/fsm_identity.c).

Three rules for the witness:

1. Declare the resolved record NOT first in the arena. For a child-instance, declare the instantiating parent module before the instantiated child module (parent first, child second); for an FSM, the header names a state that is not the first declared state (idle first, run named). A witness that declares the resolved record first lets a resolver that ignores the token text and always returns the first declared record pass undetected, because "first" already equals the resolved record's arena index.
2. Pin the resolved record's identity to its actual (non-first) arena index (the child module's id, the per-state index), not to a property a first-record resolver also happens to satisfy.
3. Prove discrimination with a mutation probe before the packet is accepted: replace the owner's token-text match with a fixed/first-record return and confirm the positive section fails. If the green section passes under the mutation, the witness does not discriminate the resolution and must be reordered (or a second reversed witness added).

A REJECTING (red) section discriminates its gate the same way: the witness must pass every check EXCEPT the obligation under test, so the mutation that removes the gate turns the section from rc == -1 to rc == 0. If a downstream check would reject the witness anyway, the gate is unproven. Precedent: the instance-lower parameter gate rejects any span text that is not plain decimal, but pigen_integer_intern_decimal (src/integer.c) independently rejects every non-digit, non-underscore character - so `#(4x)`, `#(0x4)` or `#(1'b0100)` would fail under a gate-removed mutation too and prove nothing. The discriminating witness is an underscore-grouped literal such as `#(1_000)`: intern_decimal skips underscores and interns the value, so only the digit gate's removal flips the call to success. Verify with the same mutation probe before the packet is accepted.

The FSM initial-state manifest (tests/fsm_identity_test.c FSM_WITNESS_NONFIRST, header names the second declared state) follows these rules. The accepted (i2) instance-identity manifest (tests/instance_identity_test.c) originally violated rule 1 (child declared first, so an "always first" resolver passed establish-valid-args); its correction re-declares the witness parent-first, child-second and adds a parameterized-instance witness, mirroring the FSM nonfirst pattern. The (i3) instance-lower-test lower-valid-args witness must apply the same parent-first discipline before it lands.
