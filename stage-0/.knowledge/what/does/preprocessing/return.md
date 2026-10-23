---
status: green
revised_at: "2026-09-13T15:07:29+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

pigen_preprocess_result owns written pigen_source_view plus expanded pigen_expanded_source. Written files retain raw physical tokens and exact source bytes; include sites store selected file identity and directive/path spans. Expanded source owns token/origin/macro/formal/replacement arenas. It borrows the immutable source manager. pigen_free_preprocess_result releases the result, not source ownership.
