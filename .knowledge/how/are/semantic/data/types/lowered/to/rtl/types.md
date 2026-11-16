---
status: green
revised_at: "2026-11-16T14:09:49+11:00"
---

src/rtl_lower.c:pigen_lower_rtl_type validates a semantic DATA type, derives signedness/state and packed width, lowers bounds, interns an RTL type and memoizes the semantic-to-RTL identity. Positive exact integers derive width from their magnitude; normal integral widths must evaluate to u64. Bounds use signed evaluation or lowered expressions. Invalid owners and zero RTL widths reject before publication; later failures follow how/should/compiler/builders/fail.md. Void is a zero-bit DATA marker with no RTL object type (how/are/void/data/types/represented.md).

Integral aliases use pigen_data_type_query_dimensions to collect all alias layers and the underlying vector/record atom. Layer-local getters remain unchanged for consumers that traverse layers themselves. Ordinary primitive types retain their existing path. Temporary expanded dimensions are freed on success and on bound/allocation failures.

The query includes a virtual bit range for native integer atoms. Scalar byte/shortint/integer/longint/time aliases retain their SV keyword and drop that virtual range. Additional packed dimensions require vector emission: clear the keyword and retain all dimensions, including the atom range. Direct packed extensions of native atoms use the same rule. Native SV rejects integer keywords followed by packed dimensions (/tmp/pigen-native-keyword-packed-uk73u9al). Signedness/state follow the semantic type.

src/rtl.c:pigen_rtl_type_intern compares sv_keyword along with sign/state/width/dimensions. Ignoring the keyword made scalar spelling depend on which equal-width type was lowered first. tests/rtl_lower_test.c:packed-alias-atom-widths checks all five scalar native atoms, packed aliases, direct packed extensions, exact dimension bounds and memoized no-growth. The unit gate passes 25 sections (/tmp/pigen-packed-atoms-unit3.log).

The baseline /tmp/pigen-packed-data-alias-audit-kdci7keo has 32-bit vector and record word_t[2] DATA with semantic width64 but 2-bit pins. Corrected lowering passes 72 profiles in tests/native_protocol_test.py across all AXI families, direct and internal child/routed paths. Twelve shapes cover vectors, records, alias chains, offset ranges, multidimensional counts/ranges and native atoms. Independent standard-pin scoreboards compare 64-bit values, byte projections with explicit physical positions, sidebands, stalls and reset. Fatal lint checks widths. Evidence: /tmp/pigen-packed-matrix4.log and /tmp/pigen-packed-matrix-7oxoe5pa.

Six additional ascending-range profiles pass (/tmp/pigen-packed-ascending.log). Independent ordinary-SV and emitted designs must both produce exactly ASCRANGE fatal warning categories; simulation checks actual transport/projection values. No warning is disabled.

The final compiler passes all 78 profiles under ASan/UBSan with leak detection and halt-on-error (/tmp/pigen-packed-asan-matrix.log; /tmp/pigen-packed-asan-mz6ni4q0). The full repository gate passes with the keyword interning fix (/tmp/pigen-extern-integration-verify.log). Static record declarations have their own representation (how/are/static/packed/record/declarations/emitted.md).
