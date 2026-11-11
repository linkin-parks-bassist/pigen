---
status: green
revised_at: "2026-11-11T16:16:44+11:00"
---

pigen_specialize_parameterized_instances returns an owned replacement only when assembled text differs from its input; byte-identical rendering returns NULL without an error. pigen_specialize_fixed_point repeats that step until unchanged and returns an owned final string. The production driver allows 256 rewriting rounds and 64 MiB transformed source. Exceeding either limit returns an error, never partially specialized input. These bounds constrain rounds and transformed text, not total peak allocation.

Specialized names are checked against all modules already present in each round, avoiding earlier clone names. Parameter substitution preserves identifiers immediately after a dot, including the label in .W(W), while substituting its value.

After resolving module bodies, validate_module_hierarchy in src/resolve.c checks the resolved instance graph before ownership validation and RTL lowering. An explicit DFS stack and white/active/complete colors distinguish cycles from shared acyclic children without host recursion. An edge to an active module reports recursive module instantiation cannot be elaborated at the instance span. This catches direct, mutual and specialized-clone cycles. Generate-scoped instances not yet represented in the semantic graph remain part of the generate-scope implementation audit.

make specialization-test checks fixed-point idempotence and ownership, round/size-limit failures, three recursive hierarchy diagnostics, output-file preservation on failure, and a 21-level acyclic parameterized hierarchy under backpressure. It is included in make test and make verify. Nested argument syntax and replacement-array allocation are covered by child-port-endpoint-test.

Later-round error offsets still refer to rewritten text; original-source diagnostic provenance through specialization needs auditing. Mixed-case selector scheduling and qualified cases remain separate open work.
