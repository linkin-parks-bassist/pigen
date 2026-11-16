---
status: green
revised_at: "2026-11-16T12:15:51+11:00"
---

Static packed-record signals retain their resolved field structure in emitted declarations. src/rtl_lower.c:add_named_object prepares an inline packed-struct type for a static payload object whose DATA type has a record schema. The model-owned pigen_rtl_object.declaration_type stores that finished text; expression typing and storage payload types continue to use canonical RTL types.

append_static_data_type follows alias targets to the schema owner, orders members by descending packed offsets and recursively renders nested records. Scalar member types preserve resolved state, signedness and expanded packed bounds, including negative/nonzero, ascending and multidimensional ranges. Dimensions outside a record schema remain outside its braces. Unpacked signal dimensions remain the separate object.shape suffix. Preparing inline schemas avoids depending on authored typedef visibility or generated type names.

sv_emit.c emits the prepared type before the final object name, or its ordinary RTL type when no prepared type is present. It performs no semantic lookup. rtl.c frees declaration_type alongside object name and shape. Buffered storage and native standard channel pins retain their existing packed RTL representation.

This keeps retained ordinary assignments and helper calls valid: `assign flag=local_record.body.value<0;` can access fields on the actual emitted object. Typed Pigen projections preserve named static members and their local coordinates through a separate select-lowering path (how/are/ordinary/packed/record/sub-projections/lowered.md). Retaining source text alone cannot repair a declaration flattened to a vector.

Evidence: tests/native_protocol_test.py:static_record_preservation_tests checks scalar, packed-array and unpacked-array profiles with 1,024 independently mapped values each. Scalar and packed-array tests include X/Z, signed ascending negative bounds, nested members, multidimensional nonzero packed bounds, whole-record copies and ordinary helper calls. Icarus aborts on the native pure-SV unpacked-record oracle too, so that profile uses Verilator with known values and signed descending negative bounds. Original and emitted ascending-range declarations both require fatal ASCRANGE lint; diagnostics are not disabled. data_typedef_emission_tests adds six all-family direct/child/fabric profiles reading static snapshots through retained field expressions and functions. Thirty-six native DATA profiles and focused RTL/lowering/output/emitter gates pass, including the no-search boundary check. ASan/UBSan cover these typed/retained record, native and ordered-loop profiles plus computed projections/resizing. The full `make -j4 --output-sync=target verify` gate passes (/tmp/pigen-static-member-verify-final.log).

Schema identity is owned by how/are/anonymous/packed/record/schemas/represented.md; unpacked shape by how/are/unpacked/signal/declarations/emitted.md; the terminal boundary by what/is/the/terminal/systemverilog/emitter/contract.md.
