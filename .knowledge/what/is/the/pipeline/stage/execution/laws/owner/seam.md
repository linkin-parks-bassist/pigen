---
status: green
revised_at: "2026-11-17T11:31:59+11:00"
---

include/pigen/pipeline_stage.h and src/pipeline_stage.c expose a legacy source-level summary service, pigen_pipeline_stage_laws. Production pipeline resolution in src/resolve.c uses stage-specific fields and atomic packet transfers; how/do/pipeline/stage/reads/work.md owns those semantics and independent runtime verification.

The summary contains a stage syntax ID, source-order write/read syntax ranges and incoming/write bit-width totals. Invalid summaries use PIGEN_INVALID_ID for syntax and first_write/first_read, with zero counts and widths. pigen_pipeline_stage_is_invalid recognizes that form. The service takes the semantic model, stage syntax ID, module, owning pipeline node, expanded source and output summary; success returns 0, failure returns -1 with a diagnostic and may leave partial model state.

Its four fixture constraints reject an uninitialized first-stage field read, reject later reads of fields never written earlier, restrict destinations to pipeline fields and require equal incoming/write summary widths. The skip-read anchor accepts a field written by any earlier stage. These summaries describe explicitly read/written fields and do not include production's implicit carry-forward. Their width restriction is not a production language rule.

tests/pipeline_stage_test.c and make pipeline-stage-test check these four summary constraints and covered identities/widths (what/is/the/pipeline/stage/execution/laws/test/manifest.md). They do not establish real packet storage, expression evaluation, atomic advancement or throughput. The separately tested pigen_pipeline_lower scaffold also uses placeholder stage values; what/is/the/pipeline/parent/module/lowering.md owns that boundary. Production acceptance requires the executable and independent RTL scoreboards.
