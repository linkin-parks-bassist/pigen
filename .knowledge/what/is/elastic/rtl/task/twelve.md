---
status: green
revised_at: "2026-11-11T15:36:28+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured compiler implements core datapath, pipelines, FSMs, child instances, validity actions and buffered fabric routing. The former rtl-lower/sv-emit rebaseline and biquad migration blockers are resolved; the recorded baseline owner suites and full verification passed.

Clocked case rewriting is limited to bodies identified as containing Pigen actions. Ordinary cases retain their original selector evaluation and control flow. Mixed cases use case equality (`===`), parenthesized operands, concatenated/comma-separated labels and complete if/else branch statements. Simulation witnesses cover single evaluation of an ordinary side-effecting selector, matching/default/unknown mixed selectors, concatenated labels and unbraced if/else bodies. Full `make -j8 --output-sync=target verify` passes. Remaining case gaps include mixed selector evaluation, common case width/signedness, nested case variants and bounded specialization convergence.

Use how/to/run/repository/verification.md for the verification command and what/is/the/plan.md for remaining implementation work.
