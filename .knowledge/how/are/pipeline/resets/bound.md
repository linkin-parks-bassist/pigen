---
status: green
revised_at: "2026-11-17T12:04:47+11:00"
---

pipeline_reset binds a pipeline packet and every buffered field below its field scope. src/resolve.c:apply_pipeline_reset emits flush actions for the packet and descendant buffered signals; static stage locals are excluded. The request must share the pipeline's module and clocked process. Clocked action guards preserve their authored execution position and accumulated exit/goto liveness.

A request after declaration resolves its target through normal lexical symbol lookup, then matches that exact packet symbol in the resolved-pipeline table. An unresolved request captures its expression scope in pending_pipeline_reset. When a pipeline is constructed, it binds only requests whose lookup from that saved scope selects its packet symbol. Source spelling, source spans and process identity alone cannot distinguish repeated foreach declarations. Pending requests without a matching pipeline diagnose. This preserves existing eager action ordering while isolating sibling specializations.

tests/pipeline_liveness_test.py:reset_isolation passes all channels of Stream/Lite/full AXI over 32 selective reset epochs per before/after form, alternating lanes and then clearing both. Normal and compiler ASan/UBSan runs pass (/tmp/pigen-pipeline-liveness.log; /tmp/pigen-pipeline-final-liveness-asan.log). tests/generate_semantic_test.c verifies every packet/field flush targets its foreach lane and excludes static stage locals, for both source orders (/tmp/pigen-pipeline-final-focused.log). Pending requests are applied after all versions exist and marked consumed. Production packet semantics and acceptance are owned by how/do/pipeline/stage/reads/work.md.

This binding rule does not establish unrestricted forward declaration/shadowing in arbitrary retained/named procedural scopes; that wider elaboration work remains owned by how/are/named/procedural/blocks/resolved.md and what/is/the/plan.md. Scoped pipeline construction and execution liveness are owned by how/are/generated/pipelines/resolved.md.
