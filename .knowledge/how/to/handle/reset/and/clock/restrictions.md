---
status: green
revised_at: "2026-09-13T15:07:25+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Plain always @(posedge clk) is preferred; always_ff is accepted. A conventional if(reset)/else is preserved and naturally gates actions but is optional. Declared reset connects generated storage, otherwise generated storage reset is inactive. Unbound storage, cross-domain use, multiple edges and asynchronous reset event controls diagnose at the action span.
