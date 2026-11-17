---
status: green
revised_at: "2026-11-17T11:11:54+11:00"
---

pigen_data_type_concatenation interns a type by total packed width. Reordering members without changing that width produces the same interned type, so type identity cannot establish field-to-bit order. Pipeline output aggregates likewise use an unsigned vector with the summed width of yielded fields (src/pipeline_identity.c).

The mapping follows source order: the first member occupies the most significant bits. Check expression structure or emitted values to establish this property. tests/pipeline_lower_test.c has an emitted-member-order section that lowers yield {x, y}, locates its concatenation and renders it through pigen_sv_render_expression to check source order. tests/pipeline_output_test.c also checks yielded member identities and ordering. A width-only check cannot substitute for those assertions.

Production pipelines resolve through src/resolve.c; src/pipeline.c is a quarantined textual reference. The separately tested src/pipeline_lower.c service does not establish complete production pipeline behavior. Production stage-versioning and packet behavior are owned by how/do/pipeline/stage/reads/work.md.
