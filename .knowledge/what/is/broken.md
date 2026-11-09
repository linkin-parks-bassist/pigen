---
status: green
revised_at: "2026-11-10T05:32:58+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, transfer-condition and acceptance behavior, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with guards, negated predicates, conversions, offer/fire equations and real primitives; clocked processes; inline pipelines (both stage forms); FSMs with state register/reset/guards/gotos; `if (destination <= source)` transfer-and-branch; user-callable `accepts(dst, src)`, `valid(sig)`, `ready(sig)` and `peek(expr)`. Behavioural simulations pass for a buf chain, an arithmetic pipeline, an FSM counter, the README sender FSM, and the same FSM using `accepts()`.

Defects and gaps:
- **Child-module instances and inline fabric are absent.** No instance syntax/resolution and no fabric lowering; fabric tests are not on the structured path.
- **Validity actions are missing.** `validate`, `invalidate` and `flush` are not implemented as statements; only the observation intrinsics exist.
- **Expression coverage is partial.** Bare integers, sized SV bits literals and ordinary logic/reg arithmetic work; aliases and some conditional/conversion forms remain incomplete.
- **Owner tests are partially stale.** Nine rtl-lower sections and two sv-emit sections pin the superseded contract and need rebaselining; all other structured C tests pass.
- **Examples use the legacy dialect** and need migration.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
