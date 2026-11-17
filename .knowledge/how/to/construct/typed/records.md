---
status: green
revised_at: "2026-11-17T12:40:59+11:00"
---

Use a named assignment pattern where the destination supplies a packed struct or native channel type:

```systemverilog
m.r <= '{resp: status, data: value};
when (m <= '{data: s.data, last: s.last, default: 0}) begin end
```

Names determine fields; written order does not determine bit order. Every member must appear exactly once or be covered by an explicit `default`. Unknown/duplicate fields and duplicate defaults diagnose. A scalar destination has no record schema and rejects a named pattern. Each value undergoes normal assignment conversion to its member type; explicit casts remain necessary where existing numerical/bit-vector conversion rules require them. Defaults resolve independently against each missing member's type. Unbased fills such as default:'1 repeat across destination widths; conditional arms retain that context (how/are/assignment/fill/literals/resolved.md).

Nested patterns construct packed structs through named aliases or anonymous member schemas (how/are/anonymous/packed/record/schemas/represented.md). The schema must cover the complete scalar packed layout; packed arrays of records reject rather than silently padding one element. Packing follows descending semantic offsets, sharing one mechanism between declared structs and canonical native channel layouts. The result receives the destination's alias type. Repeated projections of one buffered source remain one atomic token read through normal expression-use rules.

expression.h/expression.c represent RECORD and ordered RECORD_FIELD children with name, pattern_key and value. Packed-record resolution requires the name token; keys separately serve native collections. expression_resolve.c validates the target schema, resolves member values recursively and publishes typed concatenation/conversion. resolve.c preserves diagnostics for ordinary transfers and when actions.

Supported contexts include direct/grouped assignment RHS values, conditional arms, nested member patterns and when/FSM values. Context propagates recursively through ternaries and types both arms; compatible existing records may appear as arms. Ordinary ternaries retain intrinsic typing. A named cast also supplies context: `pair'('{high:a,low:b})`. Casts support conditional patterns, nested aliases and typed member reads; cast values participate in concatenations, replications and bitwise expressions. Normal raw-vector/numerical conversion rules still apply. Other contexts and packed-record positional conversion remain work. Native child collections use a separate identity binder (how/are/native/axi/collection/assignment/patterns/resolved.md). Owned typedefs rewrite Pigen numerical members into SV (how/are/data/typedefs/emitted.md).

The retained pre-quarantine suite checks all AXI families under stalls: defaults, signed DATA, normal/when/FSM values, malformed fields, conditional/mixed-record arms, ternaries, casts/operators and 256 nested records with conditional signed fields. Six anonymous nested-DATA reconstruction profiles also check direct and child/fabric paths with independent standard-pin scoreboards. Source declarations are owned by how/to/write/native/axi/declarations.md.
