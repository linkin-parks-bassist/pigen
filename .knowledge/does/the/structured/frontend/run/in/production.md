---
status: green
revised_at: "2026-11-09T21:26:09+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Yes. In pigen-fork the Makefile `pigen` target links src/pigen.c through pigen_compile_source and the structured owner pipeline; the classic textual CLI (`src/pigen_classic.c` and its Makefile target) is removed. Data-first inputs compile through preprocess, syntax, semantic resolution, RTL lowering, naming, output model and emission.

Remaining behavior gaps are the structured path's own: pipeline/FSM parent-module lowering is not wired, fabric and child instances are absent, and some owner tests pin the superseded storage/fire contract. See what/is/broken.md and what/is/the/plan.md.
