---
status: green
revised_at: "2026-09-13T15:07:19+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

pigen_buf input-ready is ~packet_valid | out_ready. Output payload is stored packet and valid is packet_valid. On ordinary in_valid&&in_ready it stores new payload and remains valid; otherwise accepted output clears valid. Thus a full buffer can pop/replace on one edge, while a stalled valid output holds payload.
