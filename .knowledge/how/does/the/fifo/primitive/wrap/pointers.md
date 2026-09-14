---
status: "unverified"
created_at: "2026-09-13T15:04:59+10:00"
scope: "local"
source: "rtl/pigen_primitives.sv pigen_fifo"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:19+10:00"
---

FIFO pointers compare against DEPTH-1 and wrap to zero, so non-power-of-two depths do not require spare slots. Pointer width is at least one for DEPTH<=1 and otherwise clog2(DEPTH); count width is clog2(DEPTH+1). Simultaneous push/pop preserves count. discard removes one valid head; clear resets all queue pointers/count.
