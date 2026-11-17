---
status: green
revised_at: "2026-11-17T15:49:44+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The local knowledge tree is the sole internal project documentation and architectural authority. Specification and language-law owners define intent, while executable contracts and verification owners describe current support. The plan contains remaining work. Current user instructions govern.

README.md is the public hardware-facing introduction. Its central idea is that the primitive transfer operation automatically and implicitly incorporates handshakes. Present wire/reg/logic, buf/port/fifo/skid and native AXI as transfer types in the same model. Preserve the hardware-facing structure and voice of the human README at d14aef2. Introduce native AXI module interfaces within that transfer explanation. Omit quarantined AXI fabrics from the README. Pipelines demonstrate a fixed field namespace whose names refer to successive stage registers, with immutable incoming reads and outgoing writes.

The Fabrics section leads with a complete multi-connection standalone example and explains the design-level payoff: author the connections and let Pigen generate a balanced lightweight three-port tree, buffering, local arbitration and backpressure. Make this benefit visible before implementation details. Distinguish current inline links/merges from the legacy standalone tree and the intended source-blind inline tree. The standalone path outputs/source-recognition labels violate the later blind-interface contract; avoid describing that old boundary as already hiding all metadata. The initial example must visibly show contention at shared consumers, using identical destination spellings rather than source-recognition suffixes that resemble separate ports. control_network has seven routes into three shared consumers; it passes compilation, warnings-fatal lint and SVG emission (/tmp/pigen-readme-shared-fabric-6wev9bvd).

Use the user's requested style: no scarecrowing and no parataxis. Explain hardware behavior in connected prose. Keep frontend migration reports and task narration out of the introduction. Detailed guides and the specification live in .knowledge; root SPEC.md and USER_GUIDE.md are absent.

Examples must exercise checked semantics. The pipeline example reuses x across stages; how/do/pipeline/stage/reads/work.md owns its laws and independent verification, including immutable snapshots, packet-wide advancement, reset and authored-stage latency. Current boundary and quarantine checks live in tests/native_fabric_quarantine_test.py. Retained AXI fabric examples are inactive references and must not be treated as runnable compiler acceptance. Every packet stage uses buffered storage.

Maintain answers, invariants, ownership, rationale, evidence, supported boundaries and unresolved decisions at their owners. Remove superseded facts and completed plan steps. Git owns chronology. Narrow proofs certify their assertions rather than whole prose.

Apart from the public README, avoid parallel documentation, agent_notes, root Markdown or local AGENTS.md authorities. Preserve the immutable Kestrel reference.
