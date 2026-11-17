---
status: green
revised_at: "2026-11-17T11:21:40+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pipeline fields are declared once with fixed DATA types. Each stage transforms that same namespace; changing which fields it writes does not define a new packet layout. Initialized unwritten fields carry forward.

Use ordinary typed expressions to join or split bits. For a 16-bit x and eight-bit hi/lo, initialize x in one stage, assign hi <= x[15:8] and lo <= x[7:0] in the next, then yield {hi,lo}. Each assignment uses its declared destination type and explicit selections. The stage advances its fields atomically; yield chooses the output representation and retires the incoming packet.

Production does not require the sum of explicitly written field widths to match between stages. Fields may be first initialized in later stages while other live fields carry forward. The separate pigen_pipeline_stage_laws scaffold validates equal-width input/write summaries for its own fixtures; that summary constraint is not a production language restriction. how/do/pipeline/stage/reads/work.md owns current source semantics and verification.
