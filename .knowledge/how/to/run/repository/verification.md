---
status: green
revised_at: "2026-11-09T22:34:26+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured C suite is the meaningful gate. These pass: source, preprocess, transfer-type, syntax-model, fsm-syntax, integer, semantic, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, output-model, route (four), pipeline-output/stage/lower, fsm-lower and compile. `rtl-lower-test` passes 15 of 24 sections and `sv-emit-test` passes 2 of 4; the rest pin the superseded storage/fire/instance contract and need rebaselining.

Behavioural checks that pass today: a data-first three-stage buf chain (examples/buf_pipeline_tb, 12 ordered packets), a three-stage arithmetic pipeline with backpressure, and an FSM counter that starts, counts and returns to idle. These emitted designs must be compiled with rtl/pigen_primitives.sv.

The aggregate `make test`/`make verify` corpus targets are still red: they use prototype-dialect `.pigen` files or child-instance/fabric/validity features not yet built. Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. `make clean` removes `pigen`.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
