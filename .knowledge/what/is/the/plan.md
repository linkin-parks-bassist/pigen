---
status: green
revised_at: "2026-11-10T13:55:54+11:00"
---

pigen-fork runs a structured-only compiler. `make test` and `make verify` are both fully green, including both biquad examples, all waveform gates, fabric synthesis and the smoke corpus; `fabric-merge-test` adds inline fairness coverage. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, data-first `buf` pipeline fields including typedef aliases, accepted `pipeline_reset` bindings, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions, concatenation and scalar-RHS co-slicing, nested concatenation destinations, `_` discard placeholders, packed struct typedefs with member reads and writes, retained module-scope functions/tasks and clocked processes that call them, standalone fabric synthesis with balanced three-port buffered routers, fair round-robin standalone routing, buffered skid hops, path metadata and deterministic topology SVG, inline fabric routing with a fair registered two-source merge, typed/integer parameters, `$clog2` and other constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening and signedness, indexed part-selects, one-bit primitive ties, and edge/destination diagnostics.

Remaining known spec gaps to audit and close:
1. Inline fabric fairness for more than two routed sources to one destination: two sources now use registered round-robin, but three or more still use the fixed-priority equation chain; generalize the arbiter and add a continuous-contention N-source test.
2. Macro concatenation/stringification/required-argument forms beyond current object-like/function-like expansion.
3. A process mixing a Pigen structured transfer with a retained function call currently falls back to whole-process retention, which cannot lower the Pigen construct; diagnose or support this mixed case.
4. Run a fresh requirement-by-requirement acceptance audit against `what/is/the/spec.md`, adding failing witnesses before implementation.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
