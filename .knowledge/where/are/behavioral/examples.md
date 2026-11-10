---
status: green
revised_at: "2026-11-10T16:01:14+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`examples/` contains runnable structured-Pigen pipelines and testbenches: `buf_pipeline`, `compiler_pipeline`, `guarded_pipeline`, `output_pipeline`, `port_pipeline`, `skid_pipeline`, `fifo_pipeline`, `join_pipeline`, `fixed_point_mac`, `text`, `biquad_bank` and `df1_biquad_bandpass`. Both biquad examples are migrated to the structured compiler and pass their waveform gates. `tests/` contains focused C owner tests plus Pigen/SV simulation pairs, including inline and standalone fabric fairness and topology checks. `rtl/pigen_primitives.sv` is the shared storage implementation.

Proof:

```bash
test -f examples/biquad_bank.pigen && test -f examples/df1_biquad_bandpass.pigen
```
