---
status: green
revised_at: "2026-11-11T17:14:45+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Use focused affected gates during edits and one full verification at meaningful checkpoints. verify depends on test and sv-emit-test; do not run make test followed by make verify redundantly. The explicit emitter gate covers expression rendering, literal limits, publication failure and the whole-design golden. Its two-state golden expects bit, matching the RTL records. Run make sv-emit-test T=t10-no-search when changing the emitter's structural boundary.

For this 32-logical-CPU host, build first and use bounded parallel verification with synchronized target output:

```bash
make -j8 pigen
/usr/bin/time -f 'wall=%e user=%U sys=%S' \
  make -j8 --output-sync=target verify
```

The suite covers structured C owners, pipeline corpus, fixed-point MAC, text/biquad examples, FSMs, child endpoints, inline/standalone fabric routing/fairness, SVG topology, ordinary-SV preservation and specialization. Passing gates establish their covered behavior, not completion of the full language spec. Mixed selector scheduling, qualified cases and other acceptance gaps remain in what/is/the/plan.md and what/is/broken.md.

Recorded baseline on 2026-11-10: wall 21.85 s, user 121.08 s, sys 8.35 s, exit 0. This timing predates later gate additions. Targets use unique temporary artifacts; output-test and output-waveform use separate generated SV/Verilator directories. Keep outer jobs bounded and avoid multiplying nested Verilator jobs.

Icarus Verilog 13 is at ~/.local/bin; Verilator 5.020 lint is warnings-fatal. make clean removes pigen. Investigate failures instead of weakening diagnostics. Obsolete test initializers and expectations must be reconciled with the actual record/language contract; do not change a golden merely to match unverified output.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
