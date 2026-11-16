---
status: green
revised_at: "2026-11-16T06:05:36+11:00"
---

Native child collection bindings accept positional, replicated and constant index/type/default assignment patterns. Explicit indices win over type keys, which win over default regardless of written order. The last matching type key wins. Unselected key values are not resolved. Positional and indexed entries supply one immediate endpoint/subarray; they cannot flatten or broadcast it (how/are/native/axi/collection/concatenations/resolved.md).

expression_analysis.h/expression_analysis.c expose pigen_resolve_protocol_array_binding_to_array for declared child arrays. Target dimensions supply bounds/direction; the first declared endpoint supplies the homogeneous native contract and DATA type. Legacy rank/to_shape entry points lack native target context and reject native type keys. Index keys accept elaboration expressions and parameters. Duplicate/out-of-bounds/unknown indices, duplicate defaults, uncovered elements and mixed positional/keyed syntax reject.

Simple named and package-qualified native typedef keys match the canonical complete protocol contract plus resolved DATA type. Ordinary packed typedefs and builtin scalar type names do not match native endpoints. Lexical value/parameter shadows retain index-key interpretation. Role, storage and timing remain endpoint properties, checked by child binding (how/are/native/axi/type/aliases/represented.md).

pigen_data_type_matches owns packed DATA matching, including alias transparency, state/sign, exact dimensions and named record identity; unresolved comparisons reject (how/are/packed/integral/types/matched.md).

A native type key supplies a scalar endpoint, descending into unmatched trailing dimensions. An array-valued RHS cannot satisfy a scalar native key. Defaults can supply an immediate element or descend to matching trailing subarrays. Explicit indexed rows receive their own context. Repetition retains original endpoint IDs, so multiple consuming uses reject rather than clone state.

Replication `'{COUNT{a,b}}` covers an entire dimension with positive constant COUNT; nested bodies retain immediate shape. Counts/cardinality/allocation arithmetic obey generated-size limits. Recursion is limited to 128 levels. protocol_pattern_order records index/type/default provenance; protocol_default_descent owns trailing-shape expansion.

Syntax uses ARRAY_PATTERN, RECORD/RECORD_FIELD and ARRAY_PATTERN_REPLICATION. The owned pigen_protocol_array_view creates no hardware; failures release partial views and success requires pigen_free_protocol_array_view. Child checks retain complete contract, shape, role, timing and ownership validation.

Basis: IEEE 1800-2017 §§10.9.1 and 6.22.1 ([standard](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf)). Class-qualified type keys, packed-record conversion, runtime endpoint selection and fabric pattern syntax remain unsupported. Hierarchical typedef references are forbidden by IEEE 1800-2017 §6.18; dotted generate-scope type keys are not a language gap.

Evidence: tests/native_array_test.py covers all three families under stalls/reset, precedence, unused values, contract/payload distinctions, record aliases, nested/generated/descent contexts and negative output preservation. tests/semantic_test.c checks the packed matcher. tests/generate_semantic_test.c checks repeated identities and cleanup. examples/native_axi_generated.pigen demonstrates positional/indexed patterns.
