---
status: green
revised_at: "2026-11-11T17:36:12+11:00"
---

Child ports are parent endpoints: siblings connect with `.i(a.o)`, parent transfers read `a.o` or write `b.i`, and fabrics route `a.o -> b.i`. Explicit wires remain supported. Output transfers stay explicit, e.g. `output uint[8] buf o`. Broadcast reset uses `input wire reset`; unqualified input bit remains abstract.

Resolution establishes sibling identities/omitted endpoints, static input aliases, outputs, then general inputs. Forward references and later module definitions resolve. Dot lookup distinguishes payloads from controls.

Typed endpoint_views represent aliases without storage. Output views observe original payloads; driven input views write original destinations. Undriven static projections are continuously valid unless qualified. A typed proxy preserves endpoint coordinates, signedness and state domain when the bound expression's type differs. Earlier sibling references preserve preliminary input identities so replacement cannot hide overlapping producers.

Exact aliases reuse views when type/shape match. Computed expressions also convert equal-width, equal-shape aliases back to their original expression types. Projection equality strips groups/conversions and compares ordered coordinates, normalizing range, +: and -: against packed direction. Lowering selects the owning producer independently of declaration order.

Constant slices/indices inside one output's raw-wire region map to that endpoint. Mapping requires scalar single-dimension bit coordinates and matching producer width. Whole-wire aliases also support runtime bit/indexed selections. Signed arithmetic of width max(64,index_width)+3 translates declared coordinates without truncation; reversed directions swap +: and -:. Index expressions resolve their own aliases, joining payload and index tokens atomically. Disjoint consumers still conflict because selection consumes whole tokens.

Computed inputs resolve aliases through groups, conversions, unary/binary expressions, conditionals, concatenations and peek before atomic analysis. Changed paths are rebuilt without mutating shared expressions; selections map as units. Repeated reads deduplicate, and ternaries consume all referenced sources (how/does/an/atomic/transfer/fire.md). Structural processes have no clock; cross-instance clock compatibility remains unaudited.

Symbolic controls map to parent objects; selected controls use proxy observation/drive equations. Omitted controls reuse payload_valid/payload_ready or internal nets. Explicit i_ready targets observe boundary readiness. Read-only output-ready/input-valid support manual handshakes; conflicting automatic owners diagnose. Parent-input controls are observed without drivers using pigen_lvalue_is_writable. Raw-wire writes remain legal; explicit-valid gate versus replacement remains unresolved.

Defaults zero undriven generated input payload/valid and unconsumed output ready. Equations, updates, connections and primitives count as drivers; borrowed controls retain ownership. Parent offers exclude destination readiness.

Ownership checks views and physical destinations: duplicate consumers, child/local conflicts and overlapping producers diagnose. Provably disjoint constant static projections are legal; unknown relationships conflict. Buffered tokens remain exclusive; fabrics enforce exclusivity across blocks. Static fanout/peek remain legal.

Evidence: child-port-endpoint-test covers coordinate directions, negative endpoints, signed/unsigned 128-bit indices, out-of-range indices, composite expressions, unequal-rate payload/index joins, forward references and duplicate owners. child-input-alias-ownership-test, child-slice-ownership-test, child-ready-manual-test and child-valid-manual-test cover physical/control ownership.

Remaining: nested mappings, partial-region dynamic selections, spanning producers, unresolved bounds, multidimensional mappings, local/ordinary-SV writer auditing and arrays/generate scopes. Specialization/count owners: how/does/specialization/reach/a/fixed/point.md and how/to/normalize/declaration/counts.md.
