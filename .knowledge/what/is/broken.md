---
status: green
revised_at: "2026-11-10T07:11:30+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, transfer-condition and acceptance behavior, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with guards, negated predicates, conversions, offer/fire equations and real primitives; clocked processes; inline pipelines (both stage forms); FSMs with state register/reset/guards/gotos; `if (destination <= source)` transfer-and-branch; `accepts(dst, src)`, `valid(sig)`, `ready(sig)`, `peek(expr)`; multiple modules per source; ordinary SV `assign` statements and standard child instantiations retained verbatim (`.port(expr)`). Simulations pass for a buf chain, an arithmetic pipeline, an FSM counter, the README sender FSM, an accepts()-based sender and a standard child instance.

Defects and gaps:
- **No structural child-instance metadata and no inline fabric.** Standard instantiations are preserved as source text, but instance names, child modules and port connections are not resolved into semantic records, so fabric cannot bind endpoints yet.
- **Validity actions missing.** `validate`, `invalidate` and `flush` are not implemented.
- **semantic-test is red.** Its alias conversion assertion expects IDENTITY where the ordinary-SV arithmetic extension now yields a common integer conversion; the test needs rebaselining or the resolution path needs an alias-specific identity rule.
- **Other owner tests are partially stale.** Nine rtl-lower sections and two sv-emit sections pin superseded endpoint/fire/instance or positional-instance/coverage contracts.
- **Expression coverage is partial.** Aliases and some conditional/conversion forms remain incomplete.
- **Examples use the legacy dialect** and need migration.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
