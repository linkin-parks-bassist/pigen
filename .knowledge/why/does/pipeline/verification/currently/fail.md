---
status: "unresolved"
created_at: "2026-09-14T20:43:49+10:00"
scope: "local"
source: "David's current-only knowledge instruction; current Makefile and owner contracts, Codex /root 2026-09-14"
checked_at: "2026-09-14T20:43:49+10:00"
blocker: "Icarus pipeline compilation exit139; root cause not isolated"
next_check: "In a dedicated simulation-tool investigation, capture Icarus build/version and reduce the unchanged primitive/pipeline compilation input before proposing a compiler or toolchain fix"
updated_at: "2026-09-14T20:49:23+10:00"
---

make verify currently passes all eleven structured C foundation targets and production smoke/fabric/core checks, then Icarus Verilog crashes compiling rtl/pigen_primitives.sv, /tmp/pigen-pipeline.sv and tests/pipeline_tb.sv. make pipeline-test reproduces segmentation fault and Makefile Error 139. Compiler sources and fixture inputs are unchanged from the tested baseline. The failing stage is known; the root cause is unresolved.

Blocker: Icarus crashes before pipeline simulation. Next check: record the installed Icarus version and reduce the generated pipeline/compiler input combination to isolate the crash in a separate verification investigation. Task 2 remains limited to its C RTL arena tests; no full make verify pass is claimed.
