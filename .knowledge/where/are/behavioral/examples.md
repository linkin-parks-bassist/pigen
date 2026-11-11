---
status: green
revised_at: "2026-11-11T18:18:58+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`examples/` contains runnable Pigen pipelines/testbenches: buf_pipeline, compiler_pipeline, guarded_pipeline, output_pipeline, port_pipeline, skid_pipeline, fifo_pipeline, join_pipeline, fixed_point_mac, text, biquad_bank and df1_biquad_bandpass. Both biquads pass waveform gates. tests/ contains C owners and simulation pairs, including fabrics. rtl/pigen_primitives.sv supplies shared storage.

Showcases use numerical types, always and implicit transfer guards (what/is/the/spec.md). df1_biquad_bandpass uses int[24] buffered input/output, reg history, numerical arithmetic/clamping and an atomic co-transfer. Other legacy examples need an idiomatic-Pigen audit; compatibility witnesses are not automatically showcases.

For signal processing, /tmp/pigen-filter-demo contains the canonical idiomatic filter source, emitted SV, testbench/primitives, runner, log, samples, VCD, waveform, expandable-source HTML and ZIP. Three equal-amplitude tones have periods 64/16/4 samples. All 2,048 outputs matched the integer recurrence; slow/middle/fast gains measured −11.6/+12.0/−13.7 dB for this stimulus. Plot accepted samples/spectra separately from clock stalls.

For control plus data flow, /tmp/pigen-job-demo contains jobs.pigen, emitted jobs.sv, testbench/primitives, run.sh, simulation.log, events.csv, jobs.vcd, waveform.png, index.html and ZIP. A four-entry request FIFO feeds an idle/calculate/publish FSM computing sum 1…N for nonnegative counts. Output refusal through cycle 109 fills buffering, blocks publishing and propagates backpressure; enabling the receiver drains jobs in order. Icarus checked all 12 answers, including 5→15, 8→36, 3→6 and 0→0. Plots show per-job request-to-result timing, FSM state, countdown, accumulator and handshakes. The HTML exposes Pigen/SV/testbench/log and every accepted event. Arithmetic is intentionally simple so stateful queueing behavior is recognizable.

Temporary artifacts can be regenerated; canonical filter source lives in examples/. Declaration/FSM idioms belong in how/to/write/runnable/prototype/declarations.md.

Proof:

```bash
test -f examples/biquad_bank.pigen && test -f examples/df1_biquad_bandpass.pigen
```
