---
status: green
revised_at: "2026-11-17T11:11:53+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Yes. In pigen-fork the Makefile pigen target links src/pigen.c through pigen_compile_source and the structured owner pipeline. Data-first inputs compile through preprocessing, syntax, semantic resolution, RTL lowering, naming, output model and emission. The classic textual CLI and its target are removed.

Pipelines, FSMs and transfer events resolve through src/resolve.c into the shared semantic model. Production pipelines create stage-specific field identities and one atomic packet transfer per boundary. Same-name updates, immutable incoming reads, carry-forward and final-yield equations have independent stalled simulation witnesses (how/do/pipeline/stage/reads/work.md).

FSMs resolve to one synchronous process with level-zero code, ordinary reset code and first-goto termination (how/to/write/fsm/states.md). when creates a transfer whose fire gates its body. The standalone src/pipeline_lower.c, src/pipeline_stage.c, src/fsm_lower.c and src/fsm_identity.c are independently tested structural owners rather than alternate production routes. Inline fabrics and child instances are implemented. what/is/broken.md owns current gaps; what/is/the/plan.md owns remaining work.
