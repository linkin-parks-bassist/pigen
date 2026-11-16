---
status: green
revised_at: "2026-11-17T10:17:00+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The local knowledge tree is the sole internal project documentation and architectural authority. Specification and language-law owners define intent, while executable contracts and verification owners describe current support. The plan contains remaining work. Current user instructions govern.

README.md is the public hardware-facing introduction. Its central idea is that the primitive transfer operation automatically and implicitly incorporates handshakes. Present wire/reg/logic, buf/port/fifo/skid and native AXI as transfer types in the same model. Preserve the hardware-facing structure and voice of the human README at d14aef2. Introduce AXI interfaces and fabrics within that transfer explanation. Pipelines demonstrate a fixed field namespace whose names refer to successive stage registers, with immutable incoming reads and outgoing writes.

Use the user's requested style: no scarecrowing and no parataxis. Explain hardware behavior in connected prose. Keep frontend migration reports and task narration out of the introduction. Detailed guides and the specification live in .knowledge; root SPEC.md and USER_GUIDE.md are absent.

Examples must distinguish intended semantics from checked implementation. The repeated-name pipeline is explicitly marked as currently rejected; how/do/pipeline/stage/reads/work.md owns that defect. The current packet_path, stream_stage and register_map README modules compile and pass warnings-fatal Verilator lint, and relative links resolve (/tmp/pigen-readme-transfer-naggn_tw/result.json). Previous distinct-name pipeline checks did not validate fixed-namespace behavior. The ten native example files pass compilation and fatal lint for 49 authored modules and their instantiated specializations (/tmp/pigen-publish-examples-ficaq4yn/result.json). The elastic path uses buffered storage at every packet stage.

Maintain answers, invariants, ownership, rationale, evidence, supported boundaries and unresolved decisions at their owners. Remove superseded facts and completed plan steps. Git owns chronology. Narrow proofs certify their assertions rather than whole prose.

Apart from the public README, avoid parallel documentation, agent_notes, root Markdown or local AGENTS.md authorities. Preserve the immutable Kestrel reference.
