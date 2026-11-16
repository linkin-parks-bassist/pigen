---
status: green
revised_at: "2026-11-16T14:11:08+11:00"
---

src/data_type.c owns PIGEN_DATA_TYPE_VOID as a canonical zero-bit, non-integral, non-numerical DATA marker. Its primitive spelling is void, state domain is two-state and numerical interpretation is PIGEN_NUMERICAL_NONE. pigen_data_type_is_void follows semantic aliases. Packed-width queries return zero; ordinary integer counts still require positive widths.

Void rejects packed dimensions and explicit signedness, including through semantic aliases. pigen_data_type_matches matches void only with void, transparently following aliases. Native collection type keys use that exception without treating void as a numeric wire (how/are/packed/integral/types/matched.md).

Void has no RTL object representation. pigen_lower_rtl_type rejects zero widths before publishing or memoizing an RTL type. The signal resolver rejects ordinary void objects. Packed concatenation types require integral operands; type_resolve.c gives a source diagnostic for void record fields. Source typedefs must name the complete native endpoint, such as `typedef void axi4_stream token_t;`. Standalone `typedef void name_t;` is rejected because it has no valid emitted SV representation; /tmp/pigen-void-alias-ey5yq150 contains the independent Verilator oracle and faulty baseline.

Native AXI-Stream consumes void to omit TDATA (how/to/write/axi/streams/without/data.md). AXI4-Lite/full reject zero DATA through existing width checks. Channel records retain positive internal payload storage, using one-bit internal padding when no fields exist; it creates no standard DATA pin.

Evidence: tests/rtl_lower_test.c:void-data-marker checks zero width, non-integral/non-numeric identity, semantic alias matching, invalid packed extensions/concatenation and rejection without RTL growth. Focused semantic/expression/RTL-lowering gates pass (/tmp/pigen-void-stream-units-final.log, 26 RTL-lowering sections). All 52 source profiles and eight rejection cases pass under ASan/UBSan (/tmp/pigen-void-final-asan.log). The final full repository gate passes (/tmp/pigen-void-complete-verify.log).
