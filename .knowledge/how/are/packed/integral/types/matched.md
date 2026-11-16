---
status: green
revised_at: "2026-11-16T14:05:39+11:00"
---

data_type.h/data_type.c expose pigen_data_type_matches(model,left,right): 1 means matching, 0 means different and -1 means invalid or unresolved. It compares packed integral wire types without assignment conversion. Native collection type keys use it after complete protocol matching (how/are/native/axi/collection/assignment/patterns/resolved.md).

Void is a separate non-integral marker: it matches only void, including aliases, for native no-DATA contracts (how/are/void/data/types/represented.md). Aliases are transparent. State domain, signedness, packed rank and exact left/right bounds must match. Struct schema identity is nominal: distinct declarations with identical fields differ, while aliases of the same schema match. Anonymous packed records preserve private schema owners (how/are/anonymous/packed/record/schemas/represented.md). Equal resolved wire types such as uint[32] and bit[32] match; numerical spelling alone does not distinguish them.

matching_record_owner follows alias targets to recorded struct-member owners. pigen_data_type_query_dimensions supplies owned alias-expanded packed dimensions. Equal expression IDs need no evaluation; otherwise each bound must evaluate with pigen_const_expr_evaluate_i64. The matcher releases dimension vectors on success and failure.

Nonidentical bounds beyond signed 64-bit evaluation return unresolved. Scalar/vector distinctions follow the existing dimension model. This service implements the compiler's resolved packed wire matching, rather than every IEEE 1800 type category.

Evidence: tests/semantic_test.c:assert_packed_type_matching checks alias transparency, state/sign, reversed bounds, packed rank, distinct named schemas and unresolved bounds. tests/expression_resolve_test.c checks distinct anonymous schemas and stable reuse. tests/native_array_test.py exercises matching through native child collection patterns for all AXI families. IEEE 1800-2017 §6.22.1 supplies the matching basis ([standard](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf)).
