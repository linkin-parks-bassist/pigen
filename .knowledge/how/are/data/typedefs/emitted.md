---
status: green
revised_at: "2026-11-17T10:42:43+11:00"
---

Owned scalar and packed-record DATA typedefs at compilation-unit, module and package scope emit SV declarations from parsed structure in src/output.c:append_typedef_type. Numerical `uint[n]`/`int[n]` counts become packed bit ranges with their signedness. Explicit ranges, qualifiers, named member types and package qualifiers retain their meaning; expression rendering expands exact macro token extents (how/are/macro-generated/declaration/names/preserved.md). Packed records recurse over direct member types. Native endpoint typedefs emit no packed SV declaration.

src/syntax.c gives recognized module typedefs one TYPEDEF node, without a raw RETAINED sibling. The output walk adds prepared declarations to the current module or root in source order, retaining names for ordinary helpers without duplicate declarations. Unowned SV typedefs retain authored text. Selected structured-generate typedefs use semantic emission and skip source templates (how/are/structured/generate/scopes/elaborated.md).

Flattened native pins cannot establish retained-name interoperability: a missing global typedef can leave those pins valid while `$bits(word_t)` in an ordinary helper fails. tests/native_protocol_test.py:data_typedef_emission_tests checks ten global/module helper profiles: unsigned/signed aliases, packed records, named nested records and inline nested records. Each receives warnings-fatal Verilator lint and 257 independently checked Icarus layout/signedness values. A parameterized child profile checks default and overridden member widths yielding 16-, 32- and 48-bit records. Profiles cover named/inline DATA, nested patterns, macro-defined distinct record/native aliases, retained helper snapshots and ordered local mutation, directly and through child-boundary/fabric paths for all three AXI families. Standard-pin scoreboards independently check transport and nested field/signedness values. Focused syntax/output, member-write and co-slice tests also pass.

Independent SV testbench types should be module-local when DUT types occupy compilation-unit scope. examples/biquad_bank_tb.sv retains independent layouts locally and passes the eight-filter reference model; duplicating DUT names globally is invalid SV.

Alias resolution is owned by how/are/typedef/aliases/resolved.md; record metadata by how/are/packed/struct/typedefs/represented/and/read.md. Anonymous typed projections and contextual patterns have separate schema evidence (how/are/anonymous/packed/record/schemas/represented.md); richer retained-type forms remain work.
