---
status: green
revised_at: "2026-10-01T18:30:25+10:00"
---

Current, evidence-backed inventory of what is broken, failing, incomplete, or deviating from spec in Pigen. Audited in place 2026-10-01 at master `49ab665`, re-checked after the elastic-rtl-task-5-impl-error-memo landing and the elastic-rtl-task-5-impl-exact-integer landing; every item verified against the live tree (not inferred from future contracts). Green foundation: all eleven structured C targets, fabric smoke, core-language, validate, signed-widen, ready-break, pipeline-syntax PASS. `make rtl-lower-test` is fully green (all 7 PASS lines, clean under -Werror) since the Task 5 type/expression/constant + error-rollback + memo-stability + exact-integer lowering landed on `src/rtl_lower.c`.

## Failing tests (run on master)


2. **Co-slice/slice "Pigen transfer aggregate width mismatch" `$fatal` at Time 0** — a genuine compiler defect, not the iverilog crash. The last destination of a multi-destination co-sliced transfer is assigned the FULL aggregate instead of its slice: `tests/coslice.pigen` `{left,right,state,state_delay} <= {source+1,source+2,peek(source),state}` lowers to `state_delay <= ({source+8'd1,source+8'd2,source,state})` (all 32 bits into an 8-bit dest) at generated SV line 139, and the emitted width guard `pigen_emit_width_checks` (src/assignments.c:757-772) fires `Pigen transfer aggregate width mismatch` at Time 0. Affects `coslice-test`, `slicing-test`, `signal-syntax-test`, `biquad-waveform`. Evidence: generated `/tmp/audit-coslice.sv` line 117 guard + line 139 full-width store; `make coslice-test`/`slicing-test`/`signal-syntax-test` each abort with the FATAL at Time 0. Not recorded in any current leaf (the pipeline-fail leaf names only the segfault). Deviates from spec "co-slices preserve packed bit-stream order" and the co-slice/concat law (how/to/split/or/join/packets.md).

## Environment gap (not a compiler defect)

3. **verilator is not installed** on this host. Every verilator-based target fails `Error 127`: `waveform`, `compiler-waveform`, `mac-waveform`, `join-waveform`, `fifo-waveform`, `skid-waveform`, `port-waveform`, `bram-waveform`, `guarded-waveform`, `output-waveform`, `clear-test`, `fsm-test`. `command -v verilator` → not installed; iverilog/vvp are present. These cannot be evaluated for pass/fail here.

## Unimplemented (approved, not started)

4. **Elastic RTL Tasks 6-12**: realization-owned declarations (6), whole-unit ready graph (7), atomic transfers sharing one fire (8), exact ordered output (9), terminal SV emission (10), quarantined composition/simulation (11), verification and accurate status (12). Evidence: what/is/the/plan.md "Remaining approved work"; task leaves what/is/elastic/rtl/task/six..twelve.md.

5. **Structured frontend unlinked from production `./pigen`.** The Makefile `pigen` target compiles only the textual prototype (src/pigen.c blocks/assignments/declarations/procedural/transfer/pipeline/fsm/lexer/util); it excludes the structured syntax/resolution/semantic modules (src/syntax.c, type_syntax.c, expression*.c, resolve*.c, semantic.c, rtl*.c). Evidence: does/the/structured/frontend/run/in/production.md; Makefile:11-12.

6. **Retained-core, pipeline, FSM, child-instance and fabric migration not done.** The textual prototype machinery (rewritten text, rescans, generated-name lookup, marker comments, feature-local models) must be deleted only after every retained subsystem uses one structured path. Evidence: what/are/the/remaining/architecture/migration/gates.md.

7. **Frontend gaps:** parameter/type/aggregate/array/expression forms required by accepted Pigen constructs, and macro concatenation/stringification/required arguments, are incomplete. Evidence: what/are/the/remaining/architecture/migration/gates.md.

## Unmet acceptance gate

8. **`make verify` is not clean.** Required gate "clean make verify with warnings-as-errors" is unmet: it stops at the Icarus segfault (#1) and would additionally surface #2 (width mismatch) and #3 (missing verilator). Evidence: what/are/the/remaining/architecture/migration/gates.md; this run.
