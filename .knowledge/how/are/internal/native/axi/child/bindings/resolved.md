---
status: green
revised_at: "2026-11-17T10:33:00+11:00"
---

Standalone internal AXI endpoints are forbidden. Declare AXI only on module or typed fabric input/output ports, then use child.port endpoint views in the parent. Native typedefs describe reusable port contracts but do not authorize internal bus declarations.

Instantiate a child with its AXI port unbound and drive or consume its channels through child.bus.aw/w/b/ar/r, or child.stream.beat. Named records construct requests; field projections and concatenations unpack replies into ordinary buf/fifo/skid data. Channel directions follow the child's declared boundary role. Whole compatible ports connect through named/positional child bindings, sibling views or fabric edges without intermediate native storage declarations.

src/resolve.c:add_protocol_declaration rejects internal declarations, including aliases, arrays and generated scopes. connect_protocol_aggregate requires complementary endpoint roles and complete matching contracts and timing. The owned_internal_protocol_endpoint role exception and per-side fabric ownership exceptions are removed. Parent child-port views remain storage-free; storage belongs to declared boundary ports.

tests/native_boundary_policy_test.py checks rejection with output preservation, all-family child/sibling/fabric transport, and a real Lite register-bank child driven by ordinary address tokens with DATA/RESP unpacked into ordinary replies. The native protocol and array generators retain boundary profiles and remove obsolete internal-storage modes. examples/native_axi_named_types.pigen uses direct boundary-child bindings.
