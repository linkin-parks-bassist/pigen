---
status: green
revised_at: "2026-11-11T17:28:57+11:00"
---

Child ports are parent endpoints: siblings connect with `.i(a.o)`, parent transfers read `a.o` or write `b.i`, and named/anonymous fabrics route `a.o -> b.i`. Explicit wires remain supported. Output transfers remain explicit, e.g. `output uint[8] buf o`. Broadcast reset uses `input wire reset`; unqualified input bit remains abstract.

Resolution establishes sibling identities/omitted endpoints, static input aliases, outputs, then general inputs. Forward references and later module definitions resolve. Dot lookup matches payload names; control bindings are separate.

Typed endpoint_views represent static aliases and omitted outputs with explicit controls, without storage. Output views observe original projections. Driven input views write original destinations; undriven static projections are continuously valid unless supplied valid qualifies them. Earlier sibling output references preserve preliminary input identities so replacement cannot hide overlapping producers.

Exact aliases reuse upstream views when type/shape match. Equality strips groups/conversions and compares ordered constant coordinates recursively, normalizing range, +: and -: against packed direction. Shared views preserve handshake, exclusivity and write protection; lowering selects the owning producer independently of declaration order.

A constant consumer slice/index wholly inside one output's raw-wire region maps to that producer endpoint. Mapping preserves ascending/descending and negative coordinates, requires scalar single-dimension bit coordinates with matching producer width, and supports whole-wire output bindings. The storage-free atomic transfer consumes the whole producer token even when selecting fewer bits. Disjoint consumers of that token conflict.

Computed inputs resolve aliases recursively through groups, conversions, unary/binary expressions, conditionals, concatenations and peek before atomic analysis. Rebuilding changed paths preserves shared expressions. Selections map as units; blindly replacing a base could change coordinates. Arithmetic joins sources, repeated reads deduplicate, and ternaries consume all referenced sources (how/does/an/atomic/transfer/fire.md). Structural processes have no clock; cross-instance clock compatibility remains unaudited.

Symbolic controls map to parent objects; selected controls use proxy observation/drive equations. Omitted controls reuse payload_valid/payload_ready or internal nets. Explicit i_ready targets observe boundary readiness. Read-only output-ready/input-valid bindings support manual handshakes; conflicting automatic owners diagnose. Parent-input controls are observed without drivers using pigen_lvalue_is_writable. Raw-wire payload writes remain legal; explicit-valid gate versus replacement remains unresolved.

Endpoint defaults zero undriven generated input payload/valid and unconsumed output ready. Equations, updates, connections and primitives count as drivers; borrowed controls retain ownership. Parent offers exclude their destination readiness.

Ownership checks views and physical destinations: duplicate consumers, child/local conflicts and overlapping output/driven-input producers diagnose. Provably disjoint constant static projections are legal; unknown relationships conflict. Buffered signals retain whole-token exclusivity. Fabrics enforce exclusivity across blocks; static fanout/peek remain legal.

Evidence: child-port-endpoint-test covers partial/negative/ascending aliases under stalls, arithmetic/unary/conditional/concatenated slices, unequal-rate two-producer joins and duplicate-owner errors. child-input-alias-ownership-test covers sibling/transfer/fabric writers and physical overlap; child-slice-ownership-test covers indexed ranges and buffered exclusivity. child-ready-manual-test and child-valid-manual-test cover controls.

Remaining: transformed/nested selection coordinates, selections spanning producers, symbolic bounds, multidimensional mappings, direct local/ordinary-SV writer auditing, other expressions and arrays/generate scopes. Specialization/count owners: how/does/specialization/reach/a/fixed/point.md and how/to/normalize/declaration/counts.md.
