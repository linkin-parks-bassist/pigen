---
status: green
revised_at: "2026-11-17T12:04:48+11:00"
---

src/resolve.c resolves a pipeline inside a selected generated clocked process using the process's lexical scope. The packet identity is declared there, a private field root descends from it, each boundary has a distinct version scope, and stage-local scopes see incoming versions. Local genvars, parameters, typedefs and endpoint references use normal nearest-declaration lookup.

include/pigen/pipeline_identity.h and src/pipeline_identity.c expose establish_in_scope/of_in_scope entries. A scoped identity belongs to the owning module or a descendant scope; recovery requires a direct local BUF signal bound to the requested pipeline syntax. Nested packets are INTERNAL. Module-scope wrappers retain their OUTPUT identity contract for structural-owner clients. Production adopts the resolved yield type and internal direction. The initial declaration-spelling width is provisional (what/is/the/pipeline/identifier/output/test/manifest.md).

pipeline_reset binding uses lexical packet identity and descendant field scopes (how/are/pipeline/resets/bound.md). Requests can precede or follow a declaration; unmatched targets diagnose. Qualified guards around pipelines preserve transfer/reset conditions and native clock-edge checks (how/are/qualified/if/statements/represented.md). Temporary binding tables and model actions have separate cleanup owners (how/to/call/semantic/resolution.md).

Stage locals default to static WIRE regardless of data type; explicit static annotations are accepted and buffered ones reject. Each pipeline releases its parser field-name table on success/failure, permitting independent pipelines to reuse field names. Enclosing syntax/specialization scanners recognize endpipeline (how/are/pipeline/stage/local/declarations/parsed.md).

tests/native_array_test.py checks nine qualified-guard profiles and nine explicit/implicit/parameterized-child generated profiles across Stream/Lite/full AXI with independent standard-pin scoreboards under stalls/reset (/tmp/pigen-final-generated-pipelines-vqr4nytv). Native channels travel through typed packed-record fields under normal type rules; memory-mapped channels retain independent pipelines. Root native child overrides preserve typed parameter arithmetic (how/are/generated/child/parameters/specialized.md).

resolve_procedural_statement passes statement_guard to resolve_pipeline, preserving earlier loop exits and FSM termination. Later exits preserve work constructed before them. tests/pipeline_liveness_test.py passes six before/after continue/break/goto cases with 97 packets each, X/Z controls, stalled output and resume. Fifty-four all-family array/child/routed profiles check channel queues and ingress capacity. Six reset-isolation profiles cover 32 selective epochs each. Both normal and compiler ASan/UBSan runs pass (/tmp/pigen-pipeline-liveness.log; /tmp/pigen-pipeline-final-liveness-asan.log).

tests/generate_semantic_test.c checks local widths, scoped identity recovery, before/after resets, field isolation and static-local exclusion (/tmp/pigen-pipeline-final-focused.log). examples/native_axi_generated.pigen and instantiated specializations pass warnings-fatal lint (/tmp/pigen-final-example-lint-6qi6tn_y/result.json). Production packet laws and acceptance are owned by how/do/pipeline/stage/reads/work.md; broader retained-SV elaboration remains in how/are/structured/generate/scopes/elaborated.md.
