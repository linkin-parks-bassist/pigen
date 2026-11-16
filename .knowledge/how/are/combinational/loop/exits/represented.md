---
status: green
revised_at: "2026-11-16T16:58:08+11:00"
---

Typed always_comb break and continue target the nearest containing procedural loop, including through nested blocks, if branches and case items. Break exits that loop; continue skips the remaining body and proceeds to the loop's next iteration. For FOR, continue executes the ordered step phase first. Do/while continue still evaluates the trailing condition. These statements do not target a case selection or an outer loop when an inner loop contains them. A multidimensional foreach is one loop, represented by one ordinal FOR (how/are/combinational/foreach/loops/represented.md).

src/syntax.c parses distinct PROCEDURAL_BREAK/CONTINUE nodes requiring semicolons. src/resolve.c tracks lexical combinational loop depth while resolving bodies and rejects exits without an enclosing loop. Semantic and RTL BREAK/CONTINUE nodes have no children, condition or assignment/value payload. Constructors validate that shape. FOR initializer and step blocks accept only ASSIGN children, excluding exits in header phases.

The target is structurally defined by the ordered statement tree; no source text or generated-label search recovers it. rtl_lower.c preserves the kinds. rtl.c validates loop enclosure recursively before publishing a combinational process, counting FOR's body but not initializer/step as a loop body. Terminal validation independently rejects orphan or malformed exits and invalid phase contents before output publication.

sv_emit.c carries the nearest finished RTL loop identity while traversing bodies. Each loop has private named exit and iteration-body blocks derived from that identity. Break disables the exit block; continue disables the body block. FOR steps remain after the body block, so continue reaches them and break skips them. Nested loop rendering saves the outer target naturally through recursive calls. This avoids relying on simulator support for native break/continue and keeps the phase distinction in typed IR.

Evidence: tests/generate_semantic_test.c checks exit and loop shapes; tests/rtl_test.c checks orphan-process and invalid-header rejection; tests/sv_emit_test.c checks validation before publication. tests/cores/continuous_intrinsics* exercises nested loops, dependent FOR steps, exits through cases, repeat/while exits and do/while trailing conditions with independent Icarus scoreboards, including X/Z controls. Its cores runner also compiles the authored algorithm directly as native SV and compares all results over 256 input values using warnings-fatal Verilator. tests/native_array_test.py checks all AXI families in generated/implicit/child scopes under stalls/reset and rejects orphan exits. General loop forms: how/are/combinational/loops/represented.md.

Clocked foreach exits use guarded specialization rather than this RTL loop-exit representation (how/are/clocked/foreach/loops/represented.md).
