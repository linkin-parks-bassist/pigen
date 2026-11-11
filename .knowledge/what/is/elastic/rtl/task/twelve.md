---
status: green
revised_at: "2026-11-11T15:33:38+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. The structured compiler implements core datapath, pipelines, FSMs, child instances, validity actions and buffered fabric routing. The former rtl-lower/sv-emit rebaseline and biquad migration blockers are resolved; the recorded baseline owner suites and full verification passed.

The working implementation includes preliminary clocked case-to-if rewriting and up to 16 specialization passes. Full verification currently fails at `case-guarded-mixed-error-test`, whose expected case rejection is no longer produced. Correct case semantics and replacement behavioral witnesses remain unverified; resolve this before claiming full verification green.

Use how/to/run/repository/verification.md for the verification command and what/is/the/plan.md for remaining implementation work.
