---
status: green
revised_at: "2026-11-11T18:10:33+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`examples/` contains runnable structured-Pigen pipelines/testbenches: `buf_pipeline`, `compiler_pipeline`, `guarded_pipeline`, `output_pipeline`, `port_pipeline`, `skid_pipeline`, `fifo_pipeline`, `join_pipeline`, `fixed_point_mac`, `text`, `biquad_bank` and `df1_biquad_bandpass`. Both biquads pass their waveform gates. `tests/` contains C owner tests and Pigen/SV simulation pairs, including fabric fairness/topology checks. `rtl/pigen_primitives.sv` supplies shared storage.

The showcase df1_biquad_bandpass uses int[24] buffered input/output, int[24] reg history, numerical arithmetic/clamping and always. Its atomic co-transfer advances history only when consuming a sample and emitting a result; no explicit valid guard or manual sign extension is needed. Other legacy examples still require an idiomatic-Pigen audit against what/is/the/spec.md; working compatibility syntax is not automatically a suitable showcase.

For a high-level demonstration, drive three equal-amplitude tones with periods 64/16/4 accepted samples. Compile to SV and simulate in Icarus with the primitives. Compare every output against the integer recurrence; plot accepted samples, steady-state spectra and VCD handshakes. The middle component survives; stalls propagate readiness upstream.

`/tmp/pigen-filter-demo` contains the idiomatic filter.pigen, emitted filter.sv, testbench/primitives, run.sh, simulation.log, samples.csv, filter.vcd, waveform.png, expandable-source index.html and ZIP. Its 2,048 outputs matched the recurrence; measured slow/middle/fast gains were −11.6/+12.0/−13.7 dB. These are stimulus-specific simulation measurements. Temporary artifacts can be regenerated from the canonical example.

Proof:

```bash
test -f examples/biquad_bank.pigen && test -f examples/df1_biquad_bandpass.pigen
```
