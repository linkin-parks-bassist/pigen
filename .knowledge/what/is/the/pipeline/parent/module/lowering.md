---
status: green
revised_at: "2026-11-17T11:21:40+11:00"
---

Production pipelines resolve in src/resolve.c:resolve_pipeline into common semantic signals, transfers and reset actions. Stage-specific field identities and one atomic packet transfer per boundary implement incoming snapshots, outgoing writes and carry-forward. The final yield evaluates final outgoing equations into the buffered pipeline result at that boundary. Detailed laws and independent data/latency/reset evidence belong to how/do/pipeline/stage/reads/work.md. Selected generated pipelines retain lexical identities (how/are/generated/pipelines/resolved.md).

include/pigen/pipeline_lower.h and src/pipeline_lower.c expose a separate legacy structural scaffold. Production does not call pigen_pipeline_lower. Its tests check published object counts, provenance, yielded member order and selected guard structure. The scaffold's stage equations use constant zero placeholders, and its extra boundary instances are structural records; it is not a functional lowering of source stage expressions.

tests/pipeline_lower_test.c and make pipeline-lower-test retain five structural anchors: N−1 boundary instance records, equation records, yield destination provenance, emitted concatenation member order and guard-related shape. Its negatives include first-stage field reads, unequal summary widths and a non-pipeline syntax node. These checks certify the scaffold's API, not production packet dataflow, throughput or correct stage timing.

Production acceptance uses the compiler executable and independent RTL simulations, including tests/pipeline_packet_test.py and tests/pipeline_liveness_test.py. The width-keyed type cannot establish field order; does/the/concatenation/data/type/keying/keep/member/order.md owns that distinction.
