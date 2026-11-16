---
status: green
revised_at: "2026-11-17T10:52:08+11:00"
---

Native AXI4-Stream, AXI4-Lite and full AXI4 support module/fabric input/output ports, child views and protocol-aware fabrics. Standalone internal AXI declarations reject, including native aliases, arrays, selected generate scopes and procedural bodies. Complete contracts and boundary channel storage remain native transfer semantics (what/are/the/native/axi/transfer/requirements.md).

Boundary-only compiler SHA-256: a6db03056c440b518bbbc880cb27bbd6583f39d6c1415c9f937f3f04e602089a.

Acceptance evidence:
- tests/native_boundary_policy_test.py passes 30 rejection cases, all-family child/sibling/routed transport, and 97 ordinary Lite read requests/replies through a real child with DATA/RESP unpacking, stalls and decode errors (/tmp/pigen-boundary-policy.log).
- Native declaration/typing/record/payload checks pass in phases: functions before external_axi_tests are covered by /tmp/pigen-boundary-protocol-rerun.log; the corrected external-boundary profiles and remaining main recipe pass in /tmp/pigen-boundary-protocol-tail.log. The first log terminates at an obsolete generated-external test assumption and is not an uninterrupted suite pass.
- tests/native_array_test.py passes all retained scalar/collection, generated child, typed-standalone and boundary profiles (/tmp/pigen-boundary-array-rerun.log). Internal-storage profiles were removed rather than retained as supported syntax.
- Stream merge/demux/routing, memory-mapped merge/demux, projections and real-core contention scripts exit zero in /tmp/pigen-boundary-batches-hzm9v61e/status.json. Its protocol/array/core-fabric entries contain superseded runner/fixture failures; use the dedicated passing evidence above and /tmp/pigen-boundary-core-fabric.log for 63 real-core direct/routed/multi-hop/standalone profiles.
- Generated-scope and source-reference model fixtures use ordinary local buffers and pass focused checks (/tmp/pigen-boundary-models-rerun.log and the generated-instance result in /tmp/pigen-boundary-models.log).
- All native example files compile after migrating local buses to boundary storage and child bindings (/tmp/pigen-boundary-examples-77yi5eop).

Repository-wide requalification is not established by an uninterrupted passing command. The make verify invocation in /tmp/pigen-boundary-verify-final.log ended with SIGTERM/143 after many passing targets, including 122/122 core cases. Native-protocol, generate-semantic and generated-instance were excluded and checked independently above. The remaining clocked-case recipe is independently rerun in /tmp/pigen-boundary-clocked-case.log; its exit must be checked before claiming current coverage. Earlier full-gate and sanitizer baselines predate this restriction and cannot substitute for current results.

Verified profiles include independent handshakes, complete fields, buf/fifo/skid capacity, X/Z and signed DATA, reset, exact child contracts, parameterized address maps, ID provenance, legal bursts, contention and independent switched-target progress. Limits remain exact contracts except defined ID adaptation, synchronous reset, global addresses and start-of-burst target selection. Memory-mapped and Stream routing remain separate implementations sharing fabric syntax. Broader SV work and the separate production pipeline defect are not AXI completion gates.
