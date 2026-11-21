---
status: green
revised_at: "2026-11-21T22:55:29+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The local knowledge tree is the sole internal project documentation and architectural authority. Specification and language-law owners define intent; executable contracts and verification owners describe current support. The plan contains remaining work. Current user instructions govern.

README.md is the public hardware-facing introduction. Its central idea is that primitive transfers automatically and implicitly incorporate handshakes. Present wire/reg/logic, buf/port/fifo/skid and native AXI as transfer types in the same model. Preserve the hardware-facing structure and voice of the human README at d14aef2. Introduce native AXI module interfaces within that explanation and omit quarantined AXI fabrics.

Examples should show useful computation or sequencing at a readable scale. Use plain integer literals for ordinary arithmetic where they preserve meaning; sized literals belong where width, signedness or bit patterns matter. The opening elastic datapath computes (input + 7) * 3 through buffer/skid/FIFO storage; examples/scaled_samples.pigen is its compilable source. The FSM accepts a length and emits a counted burst, advancing only on accepted output transfers; examples/burst_counter.pigen supplies the complete module. Explain when within transfers and demonstrate it in the FSM, without a separate README section. Both examples compile, pass fatal Verilator lint and have been simulated with stalled outputs and checked values/sequences. Use ordinary always @(posedge clk) for authored sequential Pigen examples; David explicitly rejects always_ff.

The atomic fan-out example declares output_packet as buf and history as fifo[8]. A transfer begin group gives both destinations each result with one source consumption; the buffer and FIFO drain independently while their shared write acceptance waits for both. Show the declarations, the ordinary always block and the two members. tests/transfer_block_test.py simulates independent stalls and checks every result through both readers.

Pipelines demonstrate a fixed field namespace whose names refer to successive stage registers, with immutable incoming reads and outgoing writes. The example reuses x across stages; how/do/pipeline/stage/reads/work.md owns its laws and independent verification, including packet-wide advancement, reset and authored-stage latency. Every packet stage uses buffered storage.

The Fabrics section leads with multiple inline connections and explains the payoff: Pigen builds a balanced lightweight three-port tree, buffering, local arbitration and backpressure. Shared consumers use identical destination port spellings. Route metadata stays internal; legacy handles and source-recognition suffixes are excluded. examples/fabric_control.pigen supplies seven routes and three shared consumers. Per-connection DATA compatibility permits mixed widths; local arbitration does not imply uniform global bandwidth. Compilation, fatal lint and diagram checks are in tests/inline_fabric_tree_test.py. Current AXI boundary/quarantine checks live in tests/native_fabric_quarantine_test.py; retained AXI fabric examples are inactive references.

Use David's requested style: no scarecrowing and no parataxis. Explain hardware behavior in connected prose. Keep frontend migration reports and task narration out of the introduction. Detailed guides and the specification live in .knowledge; root SPEC.md and USER_GUIDE.md are absent.

Maintain current answers, invariants, ownership, rationale, evidence, limits and unresolved decisions at their owners. Remove superseded facts and completed plan steps; Git owns chronology. Narrow proofs certify their assertions rather than whole prose. Apart from the public README, avoid parallel documentation, agent_notes, root Markdown or local AGENTS.md authorities. Preserve the immutable Kestrel reference.
