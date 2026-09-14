---
status: "unverified"
created_at: "2026-09-13T15:04:48+10:00"
scope: "local"
source: "include/pigen/source.h; src/source.c; tests/source_test.c"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:27+10:00"
---

Initialize a zero pigen_source_manager. pigen_source_add copies path and text immutably, returns typed source identity and indexes line starts. Spans are half-open [start,end] offsets bounded by source length; EOF zero-length spans are legal. pigen_source_locate returns one-based line/column using binary search, zero location for invalid spans. pigen_source_span_text returns borrowed bytes/length. pigen_free_sources frees owned data and zeroes the manager.
