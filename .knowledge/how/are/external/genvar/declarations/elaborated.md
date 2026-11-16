---
status: green
revised_at: "2026-11-15T09:16:05+11:00"
---

A separately declared genvar is an elaboration-only name. src/syntax.c parses `genvar row, column;` into one PIGEN_SYNTAX_GENVAR node per identifier, retaining its token/location. Declarations accept comma-separated identifiers, not initializers. Malformed/empty/trailing lists reject. Each name has a separate output spelling so one source list cannot duplicate whole declarations at emission.

src/resolve.c declares PIGEN_SYMBOL_GENVAR in the owning module or selected generated lexical scope with the existing signed 32-bit iterator type. It has no value, parameter object or runtime signal. An external-style loop initializer requires nearest visible lookup to find that kind; a missing declaration, ordinary parameter/signal or an enclosing iteration's implicit parameter does not qualify. Duplicate local declarations reject.

Loop elaboration never mutates this marker. Every selected iteration creates a fresh scope and immutable local PARAMETER with the iterator name/value, exactly as an inline declaration does (how/are/structured/generate/scopes/elaborated.md). Consequently one external genvar can be reused in separate loops while earlier iteration identities retain their values. Scoped local declarations and module declarations use ordinary shadowing. Runtime structured expressions cannot read the external marker; named iteration paths expose the immutable local parameter instead. Child-parameter overrides use those same constants.

Source specialization protects separately declared genvar names from module-parameter replacement through their containing block/module scope, including later loop controls. Inline declarations retain their loop-limited ranges. This permits a generated local genvar to shadow a module parameter without corrupting either the loop or surrounding port configuration (how/does/specialization/reach/a/fixed/point.md).

Module-level declarations emit `genvar name;` as retained declaration text, preserving ordinary SV generate regions that need them. Structured source generate templates remain skipped; their hardware comes from semantic/RTL records. No runtime counter is introduced. Ordinary retained-SV writer/name auditing remains its existing separate boundary.

Evidence: tests/generate_syntax_test.c checks list node ownership, spelling and malformed declarations. tests/generated_instance_test.c distinguishes the external marker from immutable iteration parameters and uses a named iteration path in overrides; targeted sanitizer checks pass. tests/native_array_test.py independently scores all AXI families through external module/local genvars, reuse across separate instantiation/connection loops, module-parameter shadowing and mixed native/retained ordinary-SV generation under stalls/reset. Negatives check duplicates, missing/wrong-kind declarations, nested reuse and structured runtime reads, preserving output on failure. Explicit regions and implicit if/for/case/block forms share this binding (how/are/structured/generate/constructs/parsed.md).
