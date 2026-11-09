---
status: green
revised_at: "2026-11-10T10:10:02+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured C suite is green: source, preprocess, transfer-type, syntax-model, fsm-syntax, integer, semantic, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, output-model, rtl-lower (24/24), sv-emit (all four sections), route (four), pipeline-output/stage/lower, fsm-lower and compile.

Behavioural checks that pass with bounded timeouts: the data-first buf chain, compiler/guarded/output/port/skid/fifo/join pipelines, fixed-point MAC, an FSM counter, the README sender FSM, an accepts()-based sender, a standard child instance, a two-leaf inline fabric routed link, and `concat-dest-test`. These emitted designs are compiled with rtl/pigen_primitives.sv. Router/arbitration/SVG behaviour and general concatenation/typed-parameter lowering are not yet generated.

The aggregate `make test`/`make verify` corpus targets are still red because the remaining legacy examples (`text`, `biquad_bank`, `df1_biquad_bandpass`) and fabric-router features are not built. Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. `make clean` removes `pigen`.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
