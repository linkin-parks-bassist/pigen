---
status: green
revised_at: "2026-11-10T16:35:06+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`make test` and `make verify` are fully green end to end. The suite covers the structured C owners, the data-first pipeline corpus, fixed-point MAC, text and both migrated biquad examples, FSM and child-instance checks, inline and standalone fabric fairness/router tests, SVG topology, ordinary-SV preservation witnesses, parameterized Pigen child specialization, and explicit-control elastic child ports.

Use focused affected gates during edits; run one full verification at meaningful checkpoints. `verify` already depends on `test`, so do not run `make test` and then `make verify` redundantly. For a full run on this 32-logical-CPU host, build the compiler first, then run a bounded parallel verification with synchronized target output:

```bash
make -j8 pigen
/usr/bin/time -f 'wall=%e user=%U sys=%S' \
  make -j8 --output-sync=target verify
```

Measured on 2026-11-10: wall 21.85 s, user 121.08 s, sys 8.35 s, exit 0, no failures (an earlier equivalent run measured 23.46 s wall). Every gate uses target-unique `/tmp` artifacts; `output-test` and `output-waveform` were separated to distinct generated SV and Verilator directories. Keep outer jobs bounded and do not multiply nested Verilator jobs. Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. `make clean` removes `pigen`. A failure is investigated and repaired rather than rebaselined to a weaker diagnostic unless the old assertion is obsolete by a deliberate semantic change.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
