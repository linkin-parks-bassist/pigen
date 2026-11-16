---
status: green
revised_at: "2026-11-16T14:12:50+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Public interfaces live in include/pigen/ and implementations in src/. ids.h owns distinct semantic and RTL identities.

- source.h, lexer.h and preprocess.h own source/provenance and expanded tokens.
- syntax.h, type_syntax.h and expression.h own parsed nodes/types/expressions; syntax.h also owns retained namespace scope/name arenas (how/are/retained/package/namespace/uses/resolved.md).
- data_type.h, integer.h, operation.h and transfer_type.h own type, integer, operator and realization laws. operation.h also owns pigen_case_qualifier, shared by syntax and both procedural IRs.
- semantic.h, predicate.h, expression_analysis.h, expression_resolve.h, expression_use.h and resolve.h own scopes, symbols, typed objects, expression use and semantic construction. semantic.h also exposes pigen_const_expr_evaluate_bit (how/are/constant/packed/selections/evaluated.md).
- pipeline_identity.h owns the packet signal/symbol/type/span identity. Default entries establish/access module-scope BUF outputs; scoped entries admit internal packets in module-descendant scopes (how/are/generated/pipelines/resolved.md).
- pipeline_stage.h owns a stage execution-law view: syntax ID, outgoing write and consuming read syntax IDs in source order, and incoming/outgoing packed widths. pigen_pipeline_stage_laws validates it; pigen_pipeline_stage_is_invalid checks invalid views. The implemented laws are owned by what/is/the/pipeline/stage/execution/laws/owner/seam.md.
- pipeline_lower.h owns the separately tested structural pipeline lowering service (what/is/the/pipeline/parent/module/lowering.md). Production inline expansion runs through resolve.c and common transfer/RTL services.
- fsm_identity.h owns pigen_fsm_identity: state type, initial state, state and transition target sets, span and owning syntax. pigen_fsm_identity_establish and pigen_fsm_identity_is_invalid provide construction/inspection. fsm_lower.h is a separately tested structural stage. Production FSM resolution uses resolve.c (how/to/write/fsm/states.md; how/are/generated/fsms/resolved.md).
- route.h owns retained-core direct-transfer routing: whole-unit ready-graph nodes, readiness/sizing/signedness/aggregate checks and original-span anchors, through init/free/build/check entries.

protocol.h/protocol.c own AXI4-Stream, AXI4-Lite and full AXI4 channel/field contracts, exact compatibility and role direction. semantic.h/semantic.c own canonical protocol types, endpoints, channel-signal bindings and endpoint collections. pigen_symbol_bind_protocol_type/pigen_symbol_protocol_type bind/query complete native typedef contracts (how/are/native/axi/type/aliases/represented.md). semantic.h also exposes pigen_symbol_bind_package_scope/pigen_symbol_lookup_package_member; type_resolve.h exposes pigen_resolve_type_symbol for qualified/unqualified type lookup (how/are/native/axi/package/types/represented.md). semantic.h exposes pigen_import_package, pigen_symbol_resolve_at/resolve_text_at and pigen_symbol_import_ambiguous; pigen_symbol_lookup_at/lookup_text_at inspect source-point namespaces without activation (how/are/native/axi/package/imports/resolved.md). pigen_symbol_resolve_call_text_at and pigen_retained_call_bind own retained call lookup/records (how/are/retained/package/calls/bound.md). pigen_export_package provides canonical exports through structured package syntax (how/are/native/axi/package/exports/represented.md). Native integration remains work; details: how/are/native/axi/protocol/contracts/represented.md and how/are/semantic/axi/endpoint/identities/represented.md.

semantic.h/semantic.c expose pigen_symbol_bind_procedural_scope for named-block scope identity and pigen_symbol_lookup_local for direct members (how/are/named/procedural/blocks/resolved.md), plus pigen_symbol_bind_procedural_alias for canonical static declarations shared by foreach scopes (how/are/scopes/and/symbol/bindings/represented.md).

expression_resolve.h also exposes pigen_resolve_shaped_assignment_value, with semantic/RTL ARRAY_PATTERN constructors and pigen_shape_dimension_bounds (how/are/unpacked/data/assignment/patterns/resolved.md; how/to/index/shaped/signals.md).

expression_resolve.h exposes pigen_resolve_control_expression for native control self-context (how/are/combinational/controls/resolved.md), plus pigen_resolve_case_expression and pigen_resolve_case_context for procedural comparison sizing (how/are/procedural/case/operands/resolved.md).

semantic.h/semantic.c and rtl.h/rtl.c own ordered procedural statement/child arenas and process bodies, including CASE/CASEZ/CASEX with qualifier metadata, CASE_ITEM with comma-continuation metadata, loop forms and BREAK/CONTINUE; syntax.c, resolve.c, rtl_lower.c and sv_emit.c parse, resolve, lower and render them (how/are/scoped/combinational/processes/represented.md).

expression_analysis.h owns pigen_lookup_callee_symbol for shared pure callee inspection (how/are/retained/package/subroutines/represented.md) and typed handshake CALL payloads (how/are/handshake/calls/analyzed.md), plus analyzed evaluated-value and local CASE_VALUE forms (how/are/expression/call/arguments/sized.md); semantic.h/semantic.c and rtl.h/rtl.c expose pigen_expr_add_evaluated_value and pigen_rtl_expr_add_evaluated_value for retained zero-bit evaluations (how/are/evaluated/values/represented.md).

data_type.h/data_type.c expose pigen_data_type_sv_integer for fixed 32-bit signed four-state SV integer identity, used by type inquiries (how/are/fixed-size/bit/inquiries/resolved.md). pigen_data_type_query_dimensions returns owned, alias-expanded packed query dimensions (how/are/fixed/array/inquiries/resolved.md). pigen_data_type_matches provides tri-state matching (how/are/packed/integral/types/matched.md); pigen_data_type_is_void identifies void DATA (how/are/void/data/types/represented.md). expression_analysis.h exposes pigen_resolve_protocol_array_binding_to_array for declared native child collection shape/contract context (how/are/native/axi/collection/assignment/patterns/resolved.md).

Static declaration initialization links syntax.h declarator initializer IDs, semantic.h signal initial_value expressions and existing RTL object initial_value metadata (how/are/static/procedural/initializers/represented.md). Automatic lifetime links signal_declaration.is_automatic to semantic signal is_automatic/entry_value and ordered entry assignments (how/are/automatic/procedural/initializers/represented.md).

Builder failures report and stop compilation; partial model state remains owned by the caller (how/should/compiler/builders/fail.md).
