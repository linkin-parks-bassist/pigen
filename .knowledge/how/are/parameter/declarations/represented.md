---
status: green
revised_at: "2026-11-15T08:41:44+11:00"
---

Selected child overrides create fresh module scopes and resolve dependent defaults there; typed overrides use normal assignment conversion (how/are/generated/child/parameters/specialized.md).

A structured parameter binds one typed symbol to one value expression in a module-owned lexical scope. Source declarations resolve in source order; later constants refer to parameter symbols instead of copying initializer text. A resolved expression carries type/shape/provenance/operands; constant identity is optional for general runtime expressions. Parameter-only trees point to canonical constant DAGs, while runtime signals retain invalid constant identities. Ordinary SV parameter literals/ranges use explicit SystemVerilog literal policy rather than Pigen exact-literal policy.

Separately declared genvars are value-free markers; iteration bindings are parameters (how/are/external/genvar/declarations/elaborated.md). Selected generate blocks may own local parameters. Each loop candidate receives a fresh immutable 32-bit signed genvar parameter; normalized values avoid a recursive initializer dependency across iterations. Earlier resolved references keep their original symbol/value binding. Parameter constructors validate lexical containment in the owning module. how/are/structured/generate/scopes/elaborated.md owns iteration evaluation, selection and remaining contexts; tests/generate_semantic_test.c checks local lookup and immutability.

Canonical type aliases retain both typedef symbol and stored target ID; width/state/capability/projection queries follow that target without another lookup. Module-level structured output preserves parameter names as localparams for retained SV. Generated scope values lower through semantic constants instead of copying source generate templates. how/are/parameters/preserved/for/retained/systemverilog.md owns emission and its limits.
