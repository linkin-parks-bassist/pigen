---
status: green
revised_at: "2026-11-10T16:01:09+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`make test` and `make verify` are fully green end to end. The suite covers the structured C owners (source, preprocess, transfer-type, syntax-model, fsm-syntax, integer, semantic, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, output-model, rtl-lower, sv-emit, route, pipeline-output/stage/lower, fsm-lower, compile), the data-first pipeline corpus, fixed-point MAC, text and both migrated biquad examples, FSM and child-instance checks, inline and standalone fabric fairness/router tests, SVG topology, and ordinary-SV preservation witnesses. Emitted designs compile with rtl/pigen_primitives.sv, and waveforms are simulated with bounded timeouts before compilation is treated as correctness.

Run `make test` for the fast owner corpus and `make verify` for waveform, lint-sensitive and example gates. Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. `make clean` removes `pigen`. A failure is investigated and repaired rather than rebaselined to a weaker diagnostic unless the old assertion is obsolete by a deliberate semantic change.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
