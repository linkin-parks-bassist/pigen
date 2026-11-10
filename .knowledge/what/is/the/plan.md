---
status: green
revised_at: "2026-11-10T14:14:56+11:00"
---

pigen-fork runs a structured-only compiler. `make test` and `make verify` are both fully green, including both biquad examples, all waveform gates, fabric synthesis and the smoke corpus; inline two- and N-source fabric fairness and standalone router fairness are covered by continuous-contention tests. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, data-first `buf` pipeline fields including typedef aliases, accepted `pipeline_reset` bindings, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions, concatenation and scalar-RHS co-slicing, nested concatenation destinations, `_` discard placeholders, packed struct typedefs with member reads and writes, retained module-scope functions/tasks and clocked processes that call them, a specific diagnostic for mixed function/structured processes, standalone fabric synthesis with balanced three-port buffered routers, buffered skid hops, path metadata and deterministic topology SVG, inline fabric routing with fair registered N-source merge, typed/integer parameters, `$clog2`, numeric macro token paste, and other constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening and signedness, indexed part-selects, one-bit primitive ties, and edge/destination diagnostics.

Remaining known spec gaps to audit and close:
1. Macro forms: stringification and identifier token paste. Numeric/symbol paste works with owned token spelling, but an identifier formed by paste still resolves its symbol through the original spelling span, so `my``sig` names `my`; retained items also emit macros unexpanded.
2. Run a fresh requirement-by-requirement acceptance audit against `what/is/the/spec.md`, adding failing witnesses before implementation.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
