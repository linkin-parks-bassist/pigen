---
status: green
revised_at: "2026-11-11T19:58:15+11:00"
---

Current defects, gaps and the verified working surface. Focused owners retain detailed capability boundaries (what/is/the/spec.md, how/to/connect/child/ports.md, what/is/elastic/rtl/task/five.md, how/to/run/repository/verification.md).

Working and verified: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers through the shared primitives; clocked processes; inline pipelines (both stage forms); FSMs with optional/anonymous names, ordinary level-zero code, ordinary reset code, `when` transfer events and first-executed-goto termination; `accepts`/`valid`/`ready`/`peek`; validity actions; grouped storage producers; multiple modules; ordinary assigns, child instances, generate, initial/final, always_comb/always_latch, typedefs and parameters; inline direct and skid-buffered routed links plus standalone fabric networks with deterministic SVG; concatenation destinations with `_` placeholders and nested/misaligned slices; typed and integer parameters, `$clog2` and mixed integer/logic arithmetic; ordinary interface-typed, directionless-port, enum, packed-union, unpacked-struct and bare-int-port modules emitted verbatim; asynchronous-reset and plain multi-edge always blocks and pure function-only processes retained; ordinary directives and opaque designs preserved; parameterized child specialization; implicit/explicit elastic child ports and sibling instance.port connections; safe mixed function/Pigen clocked bodies and guarded blocking dependency splits; exclusive buffered destination ownership. Mixed cases lower through native SystemVerilog case decoders; simulations cover nested/ternary/concatenated labels, widening, signedness, unknown selectors and wildcard comparisons.

Defects and gaps:
- **Ordered blocking mixed bodies remain partial.** Simple case-guarded mixed bodies lower through equality guards; general sequential case lowering remains incomplete; impure function/task side effects, loop guards and nonblocking function/Pigen write conflicts remain rejected.
- **Ordinary-SV preservation is audit-driven.** Unrecognized ordinary module items are retained, but forms beyond the witnessed assignments, declarations, generate/initial/comb, parameters, typedefs, child instances, interface/enum/union/struct modules and async-reset processes still need auditing; unsupported forms must be recovered rather than silently dropped or misdiagnosed.
- **Event and FSM limits.** `when` supports the transfer event only; other event kinds are unspecified. FSM startup without an asserted reset is an open decision. The standalone src/fsm_lower.c is a structural stage recorded by fsm-lower-test, not the production route.
- **Deferred to Pigen 1.0:** semantic ready-cycle validation and route negative witnesses. Verilator lint still exposes combinational loops, and lowering assumes validated identities.

Current red set (focused witnesses, not the whole manifest): the two coslice corpus targets fail on prototype-dialect or concatenation-value support; t9-build-walk pins a layout invariant the current walk does not meet; lint_fatal_pin looks for a removed Makefile lint flag; cli_diagnostics expects the bare tests/basic.pigen prototype form to be diagnosed, which the current product accepts.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
