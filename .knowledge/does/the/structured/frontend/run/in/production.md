---
status: green
revised_at: "2026-11-11T19:59:25+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Yes. In pigen-fork the Makefile `pigen` target links src/pigen.c through pigen_compile_source and the structured owner pipeline; the classic textual CLI (`src/pigen_classic.c` and its Makefile target) is removed. Data-first inputs compile through preprocess, syntax, semantic resolution, RTL lowering, naming, output model and emission.

Pipelines, FSMs and transfer events lower through src/resolve.c into the shared semantic model: FSMs resolve to one synchronous process with level-zero code, ordinary reset code and the first-goto termination rule (how/to/write/fsm/states.md), and `when` creates one transfer whose fire gates the body. The standalone src/pipeline_lower.c, src/pipeline_stage.c, src/fsm_lower.c and src/fsm_identity.c are structural owners recorded by their own test targets, not alternate production routes. Inline fabrics and child instances are implemented. Remaining behavior gaps belong to what/is/broken.md and the plan to what/is/the/plan.md.
