---
status: green
revised_at: "2026-11-11T15:42:25+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured compiler implements core datapath, pipelines, FSMs, child instances, validity actions and buffered fabric routing. The former rtl-lower/sv-emit rebaseline and biquad migration blockers are resolved; the recorded baseline owner suites and full verification passed.

Mixed cases lower through native SystemVerilog combinational case decoders and scalar branch guards. Native case/casez/casex semantics preserve common width/signedness, wildcard matching and branch priority. Simulations cover ordinary selector evaluation, nested/ternary/concatenated labels, optional default colons, unbraced if/else, arithmetic widening, signedness, unknown selectors and wildcard comparisons against native reference cases. Full verification passes. Mixed selector scheduling, qualified cases and specialization convergence remain open; decoders currently evaluate selectors combinationally.

Use how/to/run/repository/verification.md for the verification command and what/is/the/plan.md for remaining implementation work.
