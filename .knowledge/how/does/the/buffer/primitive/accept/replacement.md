---
status: "unverified"
created_at: "2026-09-13T15:04:59+10:00"
scope: "local"
source: "rtl/pigen_primitives.sv pigen_buf"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:19+10:00"
---

pigen_buf input-ready is ~packet_valid | out_ready. Output payload is stored packet and valid is packet_valid. On ordinary in_valid&&in_ready it stores new payload and remains valid; otherwise accepted output clears valid. Thus a full buffer can pop/replace on one edge, while a stalled valid output holds payload.
