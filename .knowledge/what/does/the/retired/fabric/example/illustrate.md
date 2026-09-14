---
status: "unverified"
created_at: "2026-09-14T20:38:51+10:00"
scope: "local"
source: "Full 2026-08-27 declaration design/seven-task plan; docs fabric source/SVG; notes invariants; retirement audit Codex /root 2026-09-14"
---

The retired readme_fabric prototype source uses PAYLOAD_W=32 and six declared connections: capture.samples.main -> processor.samples; control.settings.main --> processor.settings; processor.preview.display -> display.pixels; processor.events.logger --> logger.events; processor.metrics.monitor -> monitor.metrics; monitor.alert.logger > logger.alert. It illustrates five routed edges and one exclusive direct edge, not current target instance-port grammar.

Its generated SVG was 2183x1300 with tree-spring layout; metadata reported crossings reduced4->1 and direct crossings0. Router tree: r0.p0-r5.p1, r1.p0-r5.p2, r2.p0-r6.p1, r3.p0-r6.p2, r4.p0-r8.p2, r5.p0-r7.p1, r6.p0-r7.p2, r7.p0-r8.p1. Leaves attach display/logger at r0.p1/p2; monitor/processor at r1.p1/p2; processor/capture at r2.p1/p2; control/processor at r3.p1/p2; processor outputs twice at r4.p1/p2. Direct monitor.alert.logger->logger.alert bypasses routers. All six units and their input/output signal labels and r0-r8 were visible. SVG styling/coordinates/fonts are generated presentation, not additional language laws or compiler APIs; the preserved topology/labels/metadata answer their substance without retaining generated XML as knowledge. Reproduction depends on quarantined prototype backend and its options, whose code remains in src/.
