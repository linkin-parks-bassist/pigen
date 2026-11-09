---
status: green
revised_at: "2026-11-10T08:37:51+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior and basic inline fabric wiring, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with guards, negated predicates, conversions, offer/fire equations and real primitives; clocked processes; inline pipelines (both stage forms); FSMs with state register/reset/guards/gotos; `if (destination <= source)` transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata (named/positional, generated boundary valid/ready mapping); inline fabric `instance.port ->/> instance.port` wiring over explicit parent payload/valid/ready nets. Simulations pass for the buf chain, arithmetic pipeline, FSM counter, README sender FSM, accepts()-based sender, standard child instance, and a two-leaf fabric routed link.

Defects and gaps:
- **Fabric is wiring-only.** Routed links are combinational equations today: no balanced three-port routers, buffered ready breaks, round-robin arbitration, or SVG generation. Multiple routed sources to one destination are not arbitrated.
- **Validity actions missing.** `validate`, `invalidate` and `flush` are not implemented.
- **semantic-test is red.** Its alias conversion assertion expects IDENTITY where the ordinary-SV arithmetic extension yields a common integer conversion.
- **Other owner tests are partially stale.** Nine rtl-lower sections and two sv-emit sections pin superseded endpoint/fire/instance or positional-instance/coverage contracts.
- **Expression coverage is partial.** Aliases and some conditional/conversion forms remain incomplete.
- **Examples use the legacy dialect** and need migration.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
