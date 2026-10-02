---
status: green
revised_at: "2026-10-02T18:56:16+10:00"
---

Pigen is a real project David wants implemented well eventually, but it currently has lower priority than CointOS (the autonomous local-model software factory). For now pigen is developed by the CointOS agent pipeline, which aims to force good results on large codebases out of small local models (e.g. Qwen) running unattended 24/7, so pigen also serves as that pipeline's realistic workload. Throughput is deliberately secondary; long stretches without implementation are acceptable, although a long one is still a signal to examine.

Test contracts and implementation are separated on purpose:

- The test-writer encodes only the intended behavior, strictly, without implementation details biasing it, and with less to hold in context than writing both.
- The implementer then writes the function body quick-and-dirty and iterates against the pre-existing tests until they pass; the frozen tests force it back to the original intent.

Do not recommend merging test-writing and implementation into vertical slices as a throughput fix. Improvements should keep the separation and instead target brief feasibility, behavioral (not structural) assertions, test strictness, small-model context budget and bounded decomposition.
