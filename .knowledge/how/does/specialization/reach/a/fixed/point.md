---
status: green
revised_at: "2026-11-11T16:15:12+11:00"
---

pigen_specialize_parameterized_instances returns an owned replacement only when the assembled text differs from its input; byte-identical instance rendering returns NULL without an error. pigen_specialize_fixed_point repeats that step until unchanged and returns an owned final string. The production driver uses this owner with limits of 256 rewriting rounds and 64 MiB transformed source. Exceeding either limit returns an error, not partially specialized input. These bounds constrain rounds and transformed text, not total peak allocation.

Specialized module names are checked against all module names already present in each round, so nested expansion cannot reuse an earlier clone's name. Parameter substitution preserves identifiers immediately after a dot, including the named override label in .W(W), while substituting its value.

make specialization-test checks idempotence, fixed-point return ownership, round-limit failure, initial and expanded source-size limits, and a 21-level parameterized hierarchy with backpressure and ordered payload checks. It is included in make test and make verify. Nested list syntax and replacement-array allocation are covered by child-port-endpoint-test.

Text convergence does not prove valid module hierarchy elaboration: a recursive parameterized hierarchy can converge to cyclic clone references and still needs an elaboration-cycle audit. Later-round error offsets still refer to rewritten text; original-source diagnostic provenance through specialization needs auditing. Mixed-case selector scheduling and qualified cases remain separate open work.
