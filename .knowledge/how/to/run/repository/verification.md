---
status: green
revised_at: "2026-11-17T17:43:36+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Use focused edit gates and full checkpoints. verify includes test, sv-emit-test, cores-test, output-model-test, coslice-typedef-test, coslice-xprop-test, cli-diagnostics-test and lint-fatal-test; avoid make test before make verify. Emitter checks cover rendering, literal limits, publication failure and whole-design output. Its golden uses bit for two-state RTL; structural changes use make sv-emit-test T=t10-no-search. Smoke guard probes: how/are/clocked/controls/resolved.md.

On this 32-logical-CPU host, build first and bound parallelism with synchronized output:

```bash
make -j8 pigen
/usr/bin/time -f 'wall=%e user=%U sys=%S' \
  make -j8 --output-sync=target verify
```

make protocol-test checks protocol layouts/contracts; protocol-semantic-test checks channel identities, directions and ownership. native-protocol-test runs tests/native_fabric_quarantine_test.py: inactive fabric rejection, boundary/child lint and seven independently scored boundary transport profiles. All three run in test/verify. Native AXI fabrics are not in use. The old broad integration recipe and native-routing-compile-test are retained under archived-native-protocol-test; fabric profiles require the pre-quarantine compiler. Other active language fixtures use boundary bindings rather than inactive fabric paths. Retained native subroutine gates remain native-function-context-test and native-function-effects-test.

make fabric-test checks source-blind inline trees, router behavior, typed traffic and diagrams. fabric-compile-test checks SV/diagram ownership and accepts sanitizer CFLAGS. CLI diagnostics use the structured parser's original spans.

The suite covers structured C owners, pipeline corpus, MAC/text/biquad examples, FSMs, child endpoints, fabric routing/fairness/SVG, ordinary-SV preservation, specialization and native standard cores. cores-test runs 122 cases, twenty-six invalid parameter checks and seven SDRAM checker-rejection witnesses. Synthesizable profiles use warnings-fatal lint; legal casex/X-label tests require fatal style diagnostics (how/are/combinational/case/statements/represented.md); Icarus runs independent protocol models/scoreboards. AXI4-Lite and AXI4 each use three stall-pattern seeds; SDRAM uses three CAS/timing profiles. I2C target uses nine address/timing profiles with exhaustive address and byte checks, stretching, partial aborts, queued-byte preservation and reset. SPI target uses 12 mode/asynchronous-timing configurations with exhaustive bytes, continuous CS, late TX, receive backpressure, abort/reset recovery and first-bit hold checks. UART covers 60 framing/divisor combinations (all 30 formats at divisors 4 and 7), plus five default-8N1 divisor cases. The when-value regression checks 1536 stalled results and malformed-header failures. The concatenation regression checks 8192 bytes, whole-RHS conversion, placeholders, empty when bodies and stalls. The explicit transfer-flow example supplies independently validated movie snapshots. The integer-resize regression checks all 65536 inputs with signed/unsigned narrowing/widening, arithmetic and concatenations. make cores-report builds the evidence gallery and independently checked internal-register observer traces. Run python3 tools/cores/check_internal_players.py for browser acceptance of all native storage/views, colours, uniform clocks, capture boundaries, stable layout and inspection. Also run tools/cores/check_native_layouts.py and visually inspect all 33 internal views for clipping and overlap; make cores-video adds trace-driven clips. These gates establish covered behavior, not full language-spec completion or every separately selectable legacy witness. Remaining acceptance work is in what/is/the/plan.md and what/is/broken.md.

Recorded baseline on 2026-11-10: wall 21.85 s, user 121.08 s, sys 8.35 s, exit 0; it predates later gates. Synchronized targets buffer output. Inspect live process state before diagnosing silence. On Linux, /proc/PID/fd/1 can expose the target's spool; require test -f before reading, and never consume a live pipe. Most targets use unique temporary artifacts. output-test/output-waveform use separate generated directories. Core evidence lives in build/cores; concurrent runner invocations require distinct --out directories. Avoid multiplying nested Verilator jobs.

Icarus 13 is at ~/.local/bin and Verilator 5.020 lint is warnings-fatal. Wider-divisor oracle exception: why/does/icarus/compound/division/disagree/with/systemverilog.md. make clean removes pigen. Investigate failures without weakening diagnostics. Reconcile obsolete expectations with the language contract; never update a golden solely to match unverified output.

Verilator and Icarus are on PATH.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
