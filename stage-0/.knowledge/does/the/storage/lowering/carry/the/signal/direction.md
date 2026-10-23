---
status: green
revised_at: "2026-10-04T08:54:37+11:00"
---

Yes. The storage realization lowering (ELASTIC_SLOT, PULSE_REGISTER, PARAMETERIZED_QUEUE, SKID_QUEUE) publishes its payload object with the owner signal's semantic direction (src/rtl_lower.c storage_endpoints passes owner_signal->direction to pigen_rtl_object_add_with_owner), and pigen_signal_add (src/semantic.c) admits a concrete storage signal with any of PIGEN_SEMANTIC_INTERNAL, INPUT, OUTPUT or INOUT: for a concrete descriptor the only direction constraint is direction >= PIGEN_SEMANTIC_INTERNAL and direction <= PIGEN_SEMANTIC_INOUT (the is_concrete branch does not force INPUT). So a storage payload can carry a non-INTERNAL direction, and the test contract must build such a signal to pin payload->direction to the owner's direction rather than a hardcoded PIGEN_SEMANTIC_INTERNAL.

The endpoints record's input side is exposed as the same object for every realized declaration family: for storage the implementation sets input_payload, input_valid and input_ready all to the one payload object (the same three-object exposure the BOUNDARY, COMBINATIONAL_NET and PROCEDURAL_VARIABLE families use). A test that pins input_payload.index == payload.index (with input_valid/input_ready the same object identity) catches a direction-ignoring or input-side-dropping lowering; an idempotence-only check (comparing two records from two calls of the same lowering) does not, because both calls leave the input side equally unset.
