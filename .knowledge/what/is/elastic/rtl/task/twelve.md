---
status: green
revised_at: "2026-11-11T15:38:50+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured compiler implements core datapath, pipelines, FSMs, child instances, validity actions and buffered fabric routing. The former rtl-lower/sv-emit rebaseline and biquad migration blockers are resolved; the recorded baseline owner suites and full verification passed.

Clocked case rewriting applies to bodies identified as containing Pigen actions; ordinary cases retain original evaluation and control flow. Mixed cases use case equality and a common width/signedness context across selector and labels. Unevaluated conditional arms impose that context without evaluating the other labels. Complete statement boundaries support nested cases and unbraced if/else, with concatenated/comma-separated/ternary labels and optional default colons. Simulations cover these forms, ordinary single selector evaluation, unknown selectors, arithmetic widening and mixed signedness. Full verification passes. Mixed selector evaluation, wildcard/qualified cases and specialization convergence remain open.

Use how/to/run/repository/verification.md for the verification command and what/is/the/plan.md for remaining implementation work.
