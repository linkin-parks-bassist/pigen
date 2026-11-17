---
status: green
revised_at: "2026-11-17T12:43:40+11:00"
---

Native AXI fabrics are quarantined and not in use. src/resolve.c:add_fabric_protocol_edge rejects whole-interface native fabric edges before publication. Its former body and native lowering in src/fabric_lower.c are excluded with #if 0. The RTL primitives and integration witnesses remain available for architectural review. Module boundary declarations, child bindings and channel transfers remain active; ordinary scalar and three-port fabrics remain active.

David will review the intended use and architecture before deciding whether to reactivate, redesign or retire this work. No interconnect expansion is planned before that review. reports/axi-fabric-review.html is the requested standalone review snapshot; this leaf owns current support status.

The archived-native-protocol-test recipe retains the former broad integration suite and native-routing-compile-test. Fabric cases require a pre-quarantine compiler and are expected to reject against the current compiler. Active native-protocol-test checks quarantine diagnostics and independent boundary transport; what/is/the/native/axi/acceptance/status.md owns current verification.

## Retained inactive design

Whole-interface direct and routed links accepted parent ports, child views and matching collections, preserving channels and reversing memory-mapped B/R. Single routed links added two-entry channel skids. Complete contracts, fixed roles, translated clock/reset identities and exclusive ownership were checked before publishing scalar semantic edges with source provenance. Native local storage remained forbidden.

Stream fan-in used pigen_axis_merge with two receive slots per source. Switched graphs used source-local TDEST ordinals and beat arbitration. Exact-ID merges retained ownership until their open packet set closed. Widened TID carried source provenance (how/does/the/native/axi/stream/destination/switch/work.md; how/are/native/stream/fabric/identities/preserved.md).

Memory-mapped decoding used inclusive global address windows and pigen_axi_demux, composing five virtual channels per edge with target arbitration. Unmapped requests returned DECERR. pigen_axi_merge retained one write and an independent read per target, accepted W before AW, held write ownership through B and read ownership through RLAST. Full AXI request IDs gained source tags that were stripped on responses; target capacity was checked. Defined ID adaptation was the only contract-width adaptation (how/does/native/memory/mapped/address/routing/work.md).

Retained witnesses exercise complete fields, storage capacity, independent stalls, queued reset, AW/W ordering, legal bursts, contention, independent target progress, colliding/absent IDs and hierarchical provenance. Five/seven-source synthetic trees test cumulative tags and exclusive-monitor identity; real-core composition uses independent standard-pin/backend scoreboards (how/to/verify/native/axi/core/fabric/composition.md). These finite profiles do not establish an unrestricted interconnect architecture.

ID provenance was based on Arm IHI 0022F.b A5.3.5 and exclusive behavior on A7.2 (https://documentation-service.arm.com/static/5f915bbcf86e16515cdc3b23).
