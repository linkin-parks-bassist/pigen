---
status: "unverified"
created_at: "2026-09-13T15:05:48+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:20+10:00"
---

Transfer realization adapter. Extend rtl_lower with declaration endpoints. Abstract boundary -> payload/valid/ready ports; combinational net/procedural variable -> constant controls; elastic slot/pulse/queue/skid -> corresponding primitive structure. Dispatch private adapter table by realization, not source transfer enum. Capacity/depth/ready/occupancy/reset come from descriptor. Gate: realization matrix and no PIGEN_TRANSFER_TYPE source-enum matches in rtl_lower.c; make transfer-type-test rtl-lower-test. This is approved future work, not implemented status.

pigen_lower_rtl_module_declarations creates stable payload/valid/ready mappings. pigen_rtl_signal_endpoints stores payload object ID, valid and ready expression IDs, and input_payload/input_valid/input_ready object IDs. A constant control has invalid object identity and valid constant-expression identity. Obtain source descriptor only for realization; use a private table indexed by pigen_transfer_realization and query capacity/ready/occupancy/reset there. The matrix maps boundary to three ports, net/variable to payload plus constants, elastic/pulse/queue/skid to pigen_buf/pigen_port/pigen_fifo/pigen_skid. FIFO gets semantic depth. rg for PIGEN_TRANSFER_TYPE_(ABSTRACT|WIRE|REG|LOGIC|BUF|PORT|FIFO|SKID) in rtl_lower.c must have no matches.
