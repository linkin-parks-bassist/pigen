---
status: green
revised_at: "2026-11-17T10:16:40+11:00"
---

tests/pipeline_stage_test.c tests the stage execution-law owner in include/pigen/pipeline_stage.h and src/pipeline_stage.c through pigen_pipeline_stage_laws and pigen_pipeline_stage_is_invalid. tests/contracts.json registers four sections, selectable with make pipeline-stage-test T=<section>; omitting T runs all four. The Makefile compiles the fixture with pipeline_stage.c, pipeline_identity.c and the semantic/syntax owners under the repository C17 warning-as-error flags.

Each section pairs a rejected form with an accepted anchor and checks the resulting write/read identities, counts and incoming/outgoing widths:

- first-stage-external-reads rejects an uninitialized field read, including a <= a. A first stage reading a module input accepts with zero incoming packet bits and the written field's outgoing width.
- later-stage-incoming-immutable rejects a field never written by an earlier stage. The three-stage skip-read anchor accepts stage 3 reading a field written by stage 1, so immediately-preceding-only lookup fails this witness.
- outgoing-visibility rejects a module input or stage-local wire as a <= destination; writing a pipeline field contributes an outgoing write.
- repartition-equal-bits accepts an 8+8-bit incoming packet repartitioned into a 16-bit outgoing field and rejects narrowing a 16-bit packet to eight bits.

The fixtures parse syntax and provenance, then construct the surrounding semantic module through owner APIs. They isolate the stage-law service. Production resolve_pipeline follows a separate route and currently rejects repeated-field updates, so these tests do not certify fixed-namespace production pipelines (how/do/pipeline/stage/reads/work.md). Existing production simulations exercise distinct-name elastic behavior through stalls. Expected fixture widths come from parsed declarations.

Atomic consumption under complete guards and one-entry storage throughput require temporal lowering and behavioral tests. Multiple field writes in one stage belong to the language contract. Related owners are what/is/the/pipeline/stage/execution/laws/owner/seam.md, how/to/repartition/pipeline/boundaries.md and what/is/the/spec.md. This manifest establishes its four covered service laws.
