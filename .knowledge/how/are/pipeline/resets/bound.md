---
status: green
revised_at: "2026-11-16T18:25:10+11:00"
---

pipeline_reset binds a pipeline packet and every buffered field below its field scope. src/resolve.c:apply_pipeline_reset emits flush actions for the packet and descendant buffered signals; static stage locals are excluded. The request must share the pipeline's module and clocked process. Clocked action guards preserve their authored execution position and accumulated exit/goto liveness.

A request after declaration resolves its target through normal lexical symbol lookup, then matches that exact packet symbol in the resolved-pipeline table. An unresolved request captures its expression scope in pending_pipeline_reset. When a pipeline is constructed, it binds only requests whose lookup from that saved scope selects its packet symbol. Source spelling, source spans and process identity alone cannot distinguish repeated foreach declarations. Pending requests without a matching pipeline diagnose. This preserves existing eager action ordering while isolating sibling specializations.

The name/process-only baseline resets lane 0 when lane 1 requests a reset, both before and after declaration (/tmp/pigen-pipeline-reset-scope-probe2.log). tests/pipeline_liveness_test.py:reset_isolation passes all channels of Stream/Lite/full AXI over 32 selective reset epochs per before/after form, alternating lanes and then clearing both. Normal and compiler ASan/UBSan runs pass (/tmp/pigen-pipeline-scope-focused2.log; /tmp/pigen-pipeline-scope-asan.log). generate_semantic_test.c verifies every packet/field flush targets its foreach lane and excludes static stage locals, for both source orders (/tmp/pigen-pipeline-scope-models.log). Existing generated reset and stage-law checks pass; all four pipeline-identity selectors pass (/tmp/pigen-pipeline-scope-identity.log). Full current-source acceptance is tracked by what/is/the/native/axi/acceptance/status.md.

This binding rule does not establish unrestricted forward declaration/shadowing in arbitrary retained/named procedural scopes; that wider elaboration work remains owned by how/are/named/procedural/blocks/resolved.md and what/is/the/plan.md. Scoped pipeline construction and execution liveness are owned by how/are/generated/pipelines/resolved.md.
