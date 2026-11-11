---
status: green
revised_at: "2026-11-11T15:34:31+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured compiler implements core datapath, pipelines, FSMs, child instances, validity actions and buffered fabric routing. The former rtl-lower/sv-emit rebaseline and biquad migration blockers are resolved; the recorded baseline owner suites and full verification passed.

Clocked case-to-if rewriting uses case equality (`===`) and up to 16 specialization passes. `make -j8 --output-sync=target verify` passes, including the mixed-case simulation for matching, default and unknown selectors. General case rewriting remains preliminary: selector evaluation, complex labels, nested controls and ordinary-SV preservation need discriminating witnesses before broader correctness claims.

Use how/to/run/repository/verification.md for the verification command and what/is/the/plan.md for remaining implementation work.
