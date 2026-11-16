---
status: green
revised_at: "2026-11-16T05:40:34+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Unified declarations carry direction, structural data-type syntax ID and has_transfer_type. A written transfer occurrence separately stores identity, location and argument expression. Declarators carry name tokens and ordered shape-dimension ranges; each dimension is a count or explicit range. Multiple declarators share type/transfer/argument and own their shapes.

has_transfer_type==0 alone denotes omission; initialize its argument to invalid and do not inspect an absent occurrence's enum/location. Written no-argument transfers also have invalid argument. pigen_syntax_transfer_type_occurrence stores transfer_type, location and argument. Module declarations end at semicolons, ANSI items at top-level commas or the closing parenthesis. parse_ansi_ports groups bare-declarator-plus-shape continuations into one declaration. A new direction/type starts another declaration. Supported wire/reg prefix adapters use the same node; initial logic/bit are data types. Recognition is transactional, while decisive malformed Pigen diagnoses. Source-order eligibility and opaque typedef shadow rules govern leading transfer spellings.

src/syntax.c:module_header_has_directionless_port classifies the actual port list after skipping any parameter list. It tracks an explicit input/output/inout direction and accepts following bare declarator/shape candidates as inherited ANSI ports. It ignores nested commas in parentheses/brackets/braces. Genuine directionless/non-ANSI/interface forms retain their ordinary-SV preservation gate; inherited declarators must not make an otherwise structured module opaque. A post-parse check rejects forward native typedef uses in header type positions while preserving bare port names and ordinary interface declarations (how/are/native/axi/forward/typedef/references/diagnosed.md).

parameterized_module_has_pigen recognizes native protocols, Pigen operations/storage and uint[n]/int[n]/bit[n] declarations and casts using bounded type recognition; ordinary SV packed bit ranges, indexed values and member names do not select this path. Numerical syntax therefore prevents parameterized or retention-gated modules from silently preserving untranslated Pigen. Other retained procedural contexts still require integration (how/are/scoped/combinational/processes/represented.md).

tests/grouped_port_test.py scores 6,144 ordinary, numerical, signed, parameterized and overridden-instance values with independent unpacked shapes and fatal lint. The source specializer also recognizes numerical-only children (how/does/specialization/reach/a/fixed/point.md).

Evidence: tests/syntax_test.c checks shared declaration metadata and independent declarator shapes. tests/case_group_test.py compiles grouped ordinary ANSI ports around typed combinational cases and scores 768 selections with Icarus/fatal Verilator lint. Count validation and its independent witnesses belong to how/to/normalize/declaration/counts.md.
