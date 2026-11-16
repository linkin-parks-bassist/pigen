---
status: green
revised_at: "2026-11-15T04:13:22+11:00"
---

tests/pipeline_stage_test.c tests the stage execution-law owner in include/pigen/pipeline_stage.h and src/pipeline_stage.c through pigen_pipeline_stage_laws and pigen_pipeline_stage_is_invalid. tests/contracts.json registers four sections, selectable with `make pipeline-stage-test T=<section>`; omitting T runs all four. The Makefile compiles the fixture with pipeline_stage.c, pipeline_identity.c and the semantic/syntax owners under the repository C17 warning-as-error flags.

Each section pairs a rejected source form (return -1) with an accepted anchor (return 0), checking the resulting view's write/read identities, counts and incoming/outgoing widths:

- first-stage-external-reads rejects an uninitialized pipeline field read, including self-read `a <= a`. A first stage reading a module input is accepted with zero incoming packet bits and the written field's outgoing width.
- later-stage-incoming-immutable rejects a field never written by an earlier stage. Fields written by any earlier stage are incoming: the three-stage skip-read anchor accepts stage 3 reading a field written only by stage 1. An immediately-preceding-only implementation fails this witness.
- outgoing-visibility rejects a module input or stage-local wire as a `<=` destination; writing a pipeline field contributes one outgoing write visible to subsequent stages.
- repartition-equal-bits accepts a two-field 8+8-bit incoming packet repartitioned into one 16-bit outgoing field, and rejects a 16-bit incoming packet narrowed to an 8-bit outgoing field.

The fixtures parse pipeline/stage/assignment syntax and token provenance, then build their surrounding semantic module explicitly through owner APIs. This isolates the stage-law owner; it is not a limitation of production resolution. src/resolve.c dispatches PIGEN_SYNTAX_PIPELINE to resolve_pipeline, and the production pipeline tests exercise elastic behavior through stalls. Expected fixture widths come from parsed declarations rather than generated SV text searches.

Atomic consumption under the complete guard and one-entry storage throughput are temporal properties rather than additional source-level rejection sections. Multiple field writes in one stage remain legal. Those properties belong to pipeline lowering and behavioral tests, not this four-section contract.

Related owners: what/is/the/pipeline/stage/execution/laws/owner/seam.md, how/do/pipeline/stage/reads/work.md, how/to/repartition/pipeline/boundaries.md and what/is/the/spec.md. The manifest establishes its four covered stage laws; it does not establish full language or AXI support.
