---
status: "unverified"
created_at: "2026-09-13T15:03:20+10:00"
scope: "local"
source: "Makefile; PLAN.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:34+10:00"
---

examples/ contains runnable prototype pipelines and testbenches, including biquad_bank, fixed_point_mac, buf/compiler pipelines, df1_biquad_bandpass, joins, FIFO/skid/port/guarded/output cases. tests/ contains focused C owner tests and SV/Pigen simulation pairs. rtl/pigen_primitives.sv is the shared storage implementation. The biquad bank is the decisive planned pipeline migration target.
