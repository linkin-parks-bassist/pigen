---
status: green
revised_at: "2026-11-10T08:44:26+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured C suite is the meaningful gate. These pass: source, preprocess, transfer-type, syntax-model, fsm-syntax, integer, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, output-model, route (four), pipeline-output/stage/lower, fsm-lower and compile. `semantic-test` currently fails one alias conversion assertion after the ordinary-SV arithmetic extension. `rtl-lower-test` passes 15 of 24 sections and `sv-emit-test` passes 2 of 4; the rest pin superseded contracts.

Behavioural checks that pass today: a data-first three-stage buf chain (examples/buf_pipeline_tb), a three-stage arithmetic pipeline with backpressure, an FSM counter, the README sender FSM, an accepts()-based sender, a standard child instance, and a two-leaf inline fabric routed link (direct payload equations plus one pigen_skid per routed link). These emitted designs must be compiled with rtl/pigen_primitives.sv. Router/arbitration/SVG behaviour is not yet generated, so no router-level or SVG check exists on the structured path.

The aggregate `make test`/`make verify` corpus targets are still red: they use prototype-dialect `.pigen` files or unbuilt fabric-router/validity-action features. Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. `make clean` removes `pigen`.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
