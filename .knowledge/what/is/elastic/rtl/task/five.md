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

Type and expression owner adapters. Create rtl_lower.h/rtl_lower.c/rtl_lower_test.c and pigen_rtl_lowering. Lower type/shape/constants through data-type owner queries, memoize by identity, preserve count/range shape and explicit conversions/projections/child order. Symbol mapping is identity-based. Gate: signed/unsigned/bit widths, widening/casts/concat/selects plus invalid expression/unbound signal rollback; make rtl-test rtl-name-test rtl-lower-test. This is approved future work, not implemented status.

pigen_rtl_lowering_init(lowering, semantics, rtl) creates identity maps. pigen_lower_rtl_type and pigen_lower_rtl_expression consume validated IDs and report an error; lower constants once by identity. Signed int[8], unsigned uint[12], bit[16], widening arithmetic/cast/projection/concat tests retain state, width, signedness and every conversion. Grouping may disappear only with correct surviving provenance. Module lowering populates the symbol-to-endpoint map; an unbound signal is an error with no partial RTL records. Named files rtl_lower.h, rtl_lower.c, rtl_lower_test.c and Makefile.
