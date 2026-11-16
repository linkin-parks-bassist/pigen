---
status: green
revised_at: "2026-11-16T11:27:02+11:00"
---

Packed struct resolution preserves a private typedef schema for each declaration, including anonymous nested records. src/type_resolve.c resolves member types, forms the aggregate packed width/state, creates an alias owner in an unreachable child scope and records member names, resolved types and MSB-to-LSB offsets. This supplies existing projection, signedness, nominal matching and contextual record-construction paths without adding user-visible names or SV typedefs for the private owner.

pigen_semantic_model.record_types caches declaration identity by lexical scope, original source span and expanded token extent. Repeated resolution reuses the same type, symbols and member rows. Distinct declarations remain distinct even with identical packed layouts. Token extents distinguish multiple records within one macro invocation span; source spans distinguish separately parsed source inputs. Scope identity separates parameterized elaborations. The model owns and frees this cache.

Named typedefs retain their authored alias identities and metadata. Their anonymous underlying schema remains reachable through alias targets. pigen_struct_member_for_type follows aliases to the member owner; nested members therefore retain their own schemas instead of becoming untyped vectors. Generated native channel records use the same existing member mechanism.

Evidence: tests/expression_resolve_test.c:check_anonymous_records checks 32/16/8-bit outer/inner/member widths, signedness and offsets, repeated-resolution arena stability, private-name isolation and distinct equal-layout identities across separately parsed sources. tests/native_protocol_test.py adds standard-pin signed/field checks on inline nested DATA and six nested-pattern direct/child/fabric profiles across AXI4-Stream, AXI4-Lite and AXI4. Focused simulations, ASan/UBSan profiles and the full repository verification gate pass.

Parser member ownership and packed projection mechanics are owned by how/are/packed/struct/typedefs/represented/and/read.md; matching by how/are/packed/integral/types/matched.md.
