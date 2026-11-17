---
status: green
revised_at: "2026-11-17T12:35:10+11:00"
---

Native AXI4-Stream, AXI4-Lite and full AXI4 module boundary declarations, child bindings and channel transfers remain active. Native AXI fabrics are quarantined: direct/routed, generated and typed-standalone whole-interface links diagnose before semantic publication. Native fabric lowering is excluded from production compilation. Standalone internal AXI declarations remain forbidden.

tests/native_fabric_quarantine_test.py checks twelve rejected native fabric cases across the three families, preservation of existing output files, and positive module channel-transfer/child-binding compilation with warnings-fatal lint. make native-protocol-test runs this active gate.

The former broad native integration recipe is retained as archived-native-protocol-test. It includes inactive fabric witnesses and is not expected to pass against the quarantined compiler. Its older passing fabric evidence establishes the retained implementation's tested profile, not current feature availability. Boundary-only portions may be run independently; reactivating interconnects requires David's architectural review.

The retained fabric design and implementation locations are owned by how/are/native/axi/fabrics/connected/and/arbitrated.md. Ordinary scalar routing and three-port source-routed fabrics remain active. Standard native core verification is independent of whole-interface fabric generation. Production pipeline laws remain owned by how/do/pipeline/stage/reads/work.md.
