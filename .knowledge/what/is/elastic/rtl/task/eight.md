---
status: "unverified"
created_at: "2026-09-13T15:05:50+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:21+10:00"
---

Transfers from one fire identity. Extend rtl_lower to combine canonical guard, distinct consumer-valid and producer-ready dependencies into one fire identity shared by updates/ready equations. Lower static lvalues/projections/concat, buffered destinations whole, assignment conversion above RHS. One RTL process per semantic process. Module composition rolls back RTL and map counts entirely on failure. Gate: joins, repeated projections, static-only assignments, exclusive producers/process order; make rtl-lower-test ready-graph-test resolve-test. This is approved future work, not implemented status.

pigen_lower_rtl_transfers consumes pigen_transfer_signal_uses, lowers canonical predicate atoms and constructs one conjunction shared by destination updates and source-ready equations. Test guarded out<=left+right with distinct validity/readiness entries, repeated projections, static-only ordered updates, exclusive producers and one RTL process per semantic process. pigen_lower_rtl_module composes add-module, declarations, endpoint binding, processes/transfers and returns module identity. Any failure restores the full RTL and lowering-map counts to entry values.
