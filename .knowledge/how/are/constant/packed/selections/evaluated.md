---
status: green
revised_at: "2026-11-16T01:16:48+11:00"
---

src/semantic.c:constant_bit evaluates packed INDEX and SELECT constant nodes through their source type's declared first-dimension bounds. It maps a result bit to a source element ordinal measured from the declared right bound, then to the bit within that element. The element width comes from total packed width divided by first-dimension cardinality, preserving multidimensional element selection.

Range selects begin at the authored right coordinate. Indexed +:/-: selects derive their least-significant coordinate using the declaration's ascending/descending direction and selected count. Per-bit offsets combine slice count and result-bit position before checking signed coordinate limits, preserving valid portions of slices crossing INT64_MIN/INT64_MAX; declaration cardinality and element sizing check unsigned overflow and divisibility. An out-of-bounds coordinate yields X, including the out-of-range portion of an otherwise valid slice. An unknown index yields X for each selected bit. Unsupported/unevaluable coordinates fail rather than being treated as known values. Source X/Z bits retain their state.

pigen_const_expr_evaluate_bit exposes one in-range constant bit, including X/Z, through semantic.h. Invalid identities, missing outputs and out-of-range bit positions fail. It shares constant_bit with comparison and conversion lowering; semantic-test checks its boundary contract. pigen_const_expr_case_equal uses this per-bit owner, so constant packed selections participate in four-state comparisons without conversion to a host integer. evaluate_u64 also uses it for selection results no wider than 64 bits: every output bit must be known, and signed-negative results reject. This enables known selected constants in elaboration while retaining the API's unsigned-value contract. Wider four-state comparisons remain supported by the bit route.

Typed field projections retain the strict raw constant selection plus explicit conversion to the member type (how/are/packed/struct/typedefs/represented/and/read.md). Fill operands are normalized before selection construction, so selected widths and coordinates use context-correct values (how/are/assignment/fill/literals/resolved.md).

Evidence: tests/expression_resolve_test.c compares constant selected values against independent literal expectations and checks unsigned evaluation. Cases cover ascending/descending and negative bounds, indexed +:/-: selection, multidimensional elements, X/Z preservation, unknown indices, partially out-of-bounds slices, both signed coordinate limits and fill-derived bounds. Runtime selection behavior has an independent native-SV oracle in tests/cores/continuous_intrinsics*.
