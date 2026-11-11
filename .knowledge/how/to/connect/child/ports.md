---
status: green
revised_at: "2026-11-11T17:26:50+11:00"
---

Child ports are parent endpoints: siblings connect with `.i(a.o)`, parent transfers read `a.o` or write `b.i`, and named/anonymous fabrics route `a.o -> b.i`. Explicit wires remain supported. Output transfer types remain explicit, e.g. `output uint[8] buf o`. Broadcast reset uses `input wire reset`; unqualified input bit remains abstract.

Resolution establishes sibling identities/omitted endpoints, static input aliases, outputs, then general inputs. Forward references and later module definitions resolve. Dot lookup matches payload names; control bindings do not count as payload bindings.

Typed endpoint_views represent static aliases and omitted outputs with explicit controls, without storage. Output views observe original projections. Driven input views write their original destinations; undriven static projections are continuously valid unless supplied valid qualifies them. Earlier sibling output references preserve preliminary input identities so alias replacement cannot hide overlapping producers.

Exact aliases reuse upstream views when payload type/shape match. Equality strips groups/conversions and compares ordered constant coordinates recursively, normalizing range, +: and -: forms against packed direction. Shared views preserve handshake, exclusivity and write protection; lowering selects the owning producer independently of declaration order.

A constant consumer slice/index wholly inside one output's raw-wire region maps to a projection of that producer endpoint. Mapping preserves ascending/descending and negative coordinates, requires scalar single-dimension bit coordinates with matching producer width, and supports whole-wire output bindings. The projection becomes a storage-free atomic transfer, consuming the producer's whole token even when selecting fewer bits. Disjoint consumers of the same token still conflict.

Computed inputs such as `.i(a.o + 8'd1)` use the same atomic lowering: arithmetic joins sources, concatenations deduplicate reads, and ternaries consume all referenced sources. See how/does/an/atomic/transfer/fire.md. Structural processes have no clock; cross-instance clock compatibility remains unaudited.

Symbolic controls map to parent objects; selected controls use proxy observation/drive equations. Omitted controls reuse payload_valid/payload_ready or internal nets. Explicit i_ready targets observe actual boundary readiness. Read-only output-ready/input-valid bindings support manual handshakes; conflicting automatic owners diagnose. Parent-input controls are observed without generated drivers using pigen_lvalue_is_writable. Raw-wire payload writes remain legal; explicit-valid gate versus replacement policy remains unresolved.

Endpoint defaults zero undriven generated input payload/valid and unconsumed output ready. Equations, updates, connections and primitives count as drivers; borrowed controls retain ownership. Parent offers exclude their destination readiness.

Ownership checks views and physical destinations: duplicate consumers, child/local conflicts and overlapping output/driven-input producers diagnose. Provably disjoint constant static projections are legal; unknown relationships conflict. Buffered signals retain whole-token exclusivity. Fabrics enforce exclusivity across blocks; static fanout/peek remain legal.

Evidence: child-port-endpoint-test covers partial low/high/index, forward, whole-wire, negative/ascending aliases under stalls and duplicate producer/consumer rejection. child-input-alias-ownership-test covers sibling/transfer/fabric writers and disjoint/overlapping slices; child-slice-ownership-test covers indexed ranges and buffered exclusivity. child-ready-manual-test and child-valid-manual-test cover control ownership.

Remaining: composite/transformed partial aliases, symbolic bounds, multidimensional mappings, direct local/ordinary-SV writer auditing, other expressions and arrays/generate scopes. Specialization/count contracts belong in how/does/specialization/reach/a/fixed/point.md and how/to/normalize/declaration/counts.md.
