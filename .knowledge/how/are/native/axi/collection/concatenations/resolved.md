---
status: green
revised_at: "2026-11-15T10:38:33+11:00"
---

Child native AXI collection ports accept unpacked concatenations such as `.s({left,right})` and `.s({s[0][1],s[0][0:0],s[1]})`. Items contribute endpoints in written order and preserve each collection's declaration order. This assembles a list of independently handshaken endpoint identities; it creates no packed bus, storage or protocol adapter.

pigen_resolve_protocol_array_binding in include/pigen/expression_analysis.h and src/expression_analysis.c receives the target rank. A self-determined item of that rank contributes its leading dimension's elements; an item of rank one less contributes one endpoint or row. Trailing dimension cardinalities must agree. Scalar endpoints have rank zero. The resolver then requires target rank/cardinalities and runs complete protocol, role, timing and ownership checks for each canonical lane. Bounds may differ positionally. Repeated consuming endpoints still conflict.

These rules follow IEEE 1800-2017 section 10.10 ([IEEE text](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf)): concatenation is assignment-like and its items require self-determined types. Untyped nested concatenations, ordinary packed-data items and invalid selections reject. Positional assignment patterns instead propagate element context and support nested row construction (how/are/native/axi/collection/assignment/patterns/resolved.md). Empty collections are unsupported. Fabrics retain their coordinate-based collection selection path; concatenation does not extend fabric syntax.

The owned pigen_protocol_array_view has an invalid single-source array ID for a construction, copied trailing dimensions, a descending synthetic leading dimension and canonical endpoint IDs. Its size obeys the generated-size policy with checked allocation arithmetic. Free it with pigen_free_protocol_array_view; failure releases partial items and accumulated tables. Selection mechanics: how/are/native/axi/collection/selections/resolved.md.

Evidence: tests/native_array_test.py independently scores scalar, mixed slice, multidimensional row and child-collection concatenations for Stream, Lite and full AXI under stalls and queued reset. Negatives cover sizes, rank, trailing dimensions, ordinary items, nested braces, protocol, role and duplicate ownership. tests/generate_semantic_test.c checks successful construction and cleanup after rank/policy failures. examples/native_axi_generated.pigen includes native_axis_joined_ports.
