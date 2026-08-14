This repo is me taking my frustration with Verilog out by trying to make it better. Consult the README.md and SPEC.md files. You will also likely find *PLAN*.md files, which I will likely want you to follow

Pigen is pre-release. There is no legacy Pigen compatibility to preserve. A
change replaces the previous Pigen syntax, implementation, examples, and tests
outright. Do not retain deprecated forms, dual implementations, feature flags,
fallbacks, or compatibility paths. Keep the codebase singular and clean.

This does not weaken the distinct ordinary-SystemVerilog compatibility contract
in SPEC.md: accepted non-Pigen SystemVerilog must retain its behavior.
