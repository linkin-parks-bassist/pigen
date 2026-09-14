---
status: "unverified"
created_at: "2026-09-13T15:03:11+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:25+10:00"
---

Plain always @(posedge clk) is preferred; always_ff is accepted. A conventional if(reset)/else is preserved and naturally gates actions but is optional. Declared reset connects generated storage, otherwise generated storage reset is inactive. Unbound storage, cross-domain use, multiple edges and asynchronous reset event controls diagnose at the action span.
