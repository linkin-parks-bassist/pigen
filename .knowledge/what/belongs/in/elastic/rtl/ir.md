---
status: green
revised_at: "2026-11-16T03:17:52+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The implemented RTL arenas represent modules, ports/nets/variables/parameters, primitive and generic instances with ordered parameters/connections, typed expressions, equations, clocked processes with enabled updates, and combinational processes with ordered block/if/case/loop/blocking-assignment trees with lexical BREAK/CONTINUE exits. Packed layout, bounds, signedness, state domain and conversions are explicit. ARRAY_PATTERN expressions preserve ordered unpacked elements for contextual native emission, separately from packed concatenations (how/are/unpacked/data/assignment/patterns/resolved.md). Owner ranges and references describe hardware identity; semantic IDs and source spans provide provenance.

Owned final names, generic module definitions, port names and source-to-emitted reference metadata belong in RTL. Retained opaque SV belongs to the source/output owners. Hardware behavior comes from typed records rather than reparsed source spellings or generated text.

Records and constructors: include/pigen/rtl.h and src/rtl.c. Ownership: what/is/elastic/rtl/task/three.md. Procedural trees: how/are/scoped/combinational/processes/represented.md. Terminal consumption: what/is/the/terminal/systemverilog/emitter/contract.md.
