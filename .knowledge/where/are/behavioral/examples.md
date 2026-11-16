---
status: green
revised_at: "2026-11-16T06:08:12+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`examples/` contains runnable Pigen pipelines/testbenches: buf_pipeline, compiler_pipeline, guarded_pipeline, output_pipeline, port_pipeline, skid_pipeline, fifo_pipeline, join_pipeline, fixed_point_mac, text, biquad_bank and df1_biquad_bandpass. Both biquads pass waveform gates. tests/ contains C owners and simulation pairs, including fabrics. rtl/pigen_primitives.sv supplies shared storage.

Showcases use numerical types, always and implicit transfer guards (what/is/the/spec.md). df1_biquad_bandpass uses int[24] buffered input/output, reg history, numerical arithmetic/clamping and an atomic co-transfer. Other legacy examples need an idiomatic-Pigen audit; compatibility witnesses are not automatically showcases.

For source-visible transfer motion, examples/transfer_flow.pigen explicitly forks each N into left/right buf slots, computes 2N and N+1 and joins them into an egress buf. A conditional right path and blocked receiver demonstrate backpressure. tests/cores/transfer_flow_tb.sv checks inputs 2,5,9,3,7,1 produce 7,16,28,10,22,4 in order, held output stability and buffer transitions. The example contains only its transfer datapath; there is no joined counter. Pre-edge slots.csv captures occupancy, data and actual fire events; tools/cores/transfer_trace.py independently checks each transition and atomic endpoint conditions. build/cores/index.html exposes exact source, simulation evidence and media/transfer_slots.mp4. This is the transfer animation example because its drawn connections correspond directly to native operations, without exposing intrinsic FIFO internals.

For signal processing, /tmp/pigen-filter-demo contains the canonical idiomatic filter source, emitted SV, testbench/primitives, runner, log, samples, VCD, waveform, expandable-source HTML and ZIP. Three equal-amplitude tones have periods 64/16/4 samples. All 2,048 outputs matched the integer recurrence; slow/middle/fast gains measured −11.6/+12.0/−13.7 dB for this stimulus. Plot accepted samples/spectra separately from clock stalls.

For control plus data flow, examples/job_engine.pigen and examples/job_engine_tb.sv implement a queued worker. Its anonymous fsm uses when for capture/publication and ordinary leading reset goto. A four-entry request FIFO feeds idle/calculate/publish states computing sum 1…N; blocked output propagates backpressure. Icarus checks 12 answers including 5→15, 8→36, 3→6, 0→0. /tmp/pigen-job-demo contains source, emitted SV, reproduction scripts, events.csv, VCD, plots, expandable-source HTML and ZIP. The renderer discovers the generated FSM state name and plots request/result timing, states, countdown, accumulator and handshakes. run.sh compiles jobs.pigen through ${PIGEN:-pigen}; render.py rebuilds plots/HTML/ZIP from simulation.

examples/native_axi_generated.pigen contains all-family generated AXI banks, selected child/fabric implementations, native_axis_fsm_bank, native_axis_pipeline_bank, native_axis_array_link and native_axis_row_bank. native_axis_observed_bank demonstrates always_comb observations and case-selected block-local flag updates (how/are/scoped/combinational/processes/represented.md). The FSM bank gives each lane a startup counter and anonymous controller; how/are/generated/fsms/resolved.md owns its scope semantics and independent transport evidence. The pipeline bank transports a packed beat through lane-local fields with a local reset binding (how/are/generated/pipelines/resolved.md). The array link connects matching collections through one fabric statement. The row bank binds each generated child to one selected parent row (how/are/native/axi/collection/selections/resolved.md). native_axis_joined_ports assembles two independent scalar ports into each child collection using concatenations (how/are/native/axi/collection/concatenations/resolved.md). native_axis_pattern_bank uses positional patterns in each generated child's row binding; native_axis_keyed_ports uses explicit coordinates and a default (how/are/native/axi/collection/assignment/patterns/resolved.md).

examples/native_axi_relay.pigen includes native_axis_switch2x3 with mixed root/generated links and source-local TDEST ordinals (how/does/the/native/axi/stream/destination/switch/work.md), native_axi4_switch2x2 with generated two-source/two-target inclusive address windows and widened target IDs, and native_axil_switch2 with two disjoint windows; both memory-mapped examples expose BASE0, BASE1 and WINDOW_BYTES parameters (how/does/native/memory/mapped/address/routing/work.md).

Package contracts: examples/native_axi_packages.pigen (how/are/native/axi/package/types/represented.md).

Regenerate temporary artifacts from canonical examples/. Declaration/FSM idioms belong in how/to/write/runnable/prototype/declarations.md. Library reproduction belongs in how/to/create/standard/core/test/artifacts.md.

Proof:

```bash
test -f examples/biquad_bank.pigen && test -f examples/df1_biquad_bandpass.pigen
```
