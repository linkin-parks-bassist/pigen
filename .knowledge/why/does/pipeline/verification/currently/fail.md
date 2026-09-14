---
status: "unresolved"
created_at: "2026-09-14T20:43:49+10:00"
scope: "local"
source: "Fresh make verify and focused make pipeline-test logs 2026-09-14; git diff --exit-code for src/include/tests/Makefile/rtl/examples; eleven PASS results and independent make rtl-test"
checked_at: "2026-09-14T20:43:49+10:00"
blocker: "Icarus pipeline compilation exit139; root cause not isolated"
next_check: "In a dedicated simulation-tool investigation, capture Icarus build/version and reduce the unchanged primitive/pipeline compilation input before proposing a compiler or toolchain fix"
---

During the 2026-09-14 document cutover, make verify passed all eleven structured C foundation targets plus production smoke/fabric/core checks, then Icarus Verilog crashed while compiling rtl/pigen_primitives.sv, /tmp/pigen-pipeline.sv and tests/pipeline_tb.sv. make pipeline-test reproduced Segmentation fault and Makefile Error 139. The owning compiler sources, headers, tests, primitives, examples and Makefile have no diff from baseline 30ac091. The observed failing stage is established; its root cause is not. No compiler fix was attempted during document retirement and no full make verify pass is claimed. Task 2 uses C-only rtl-test and should not expand into this unrelated simulation diagnosis.
