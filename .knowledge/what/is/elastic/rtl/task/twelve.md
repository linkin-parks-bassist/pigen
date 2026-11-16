---
status: green
revised_at: "2026-11-16T17:39:56+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured compiler implements core datapaths, pipelines, FSMs, child instances, validity actions and buffered fabric routing. Full verification of clocked cases and foreach exits passes; native acceptance and the current integration gate are owned by what/is/the/native/axi/acceptance/status.md.

Pure clocked case/casez/casex use typed matches and edge-qualified checks (how/are/clocked/case/statements/represented.md). Typed combinational cases preserve unique/unique0/priority and authored defaults (how/are/combinational/case/statements/represented.md). Existing unqualified function/transfer compositions retain selective native SystemVerilog combinational decoders and scalar guards. Their regressions compare selector evaluation, nested/ternary/concatenated labels, default colons, unbraced if/else, widening, signedness and unknown/wildcard matching against independent native references. Effectful mixed-case scheduling and qualification remain open; legacy decoders evaluate selectors combinationally.

Use how/to/run/repository/verification.md for verification commands and what/is/the/plan.md for remaining implementation work.
