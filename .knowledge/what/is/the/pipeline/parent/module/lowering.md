---
status: green
revised_at: "2026-11-15T09:47:45+11:00"
---

Production inline pipelines expand in src/resolve.c into common semantic signals, transfers and actions, then use the normal incidence/RTL/output path. Selected generated pipelines use lexical packet/field/stage scopes (how/are/generated/pipelines/resolved.md). Production does not call the separate pigen_pipeline_lower service merely because a pipeline syntax node exists.

include/pigen/pipeline_lower.h and src/pipeline_lower.c provide the separately tested structural lowering service. pigen_pipeline_lower(lowering,module,syntax,pipeline,expanded) consumes the module-scope outgoing packet identity through pigen_pipeline_identifier_of, validates stage laws through pigen_pipeline_stage_laws, and publishes common RTL records in the parent module. It implements one-entry BUF stage storage, combinational next-value equations over immutable incoming fields, complete procedural guard gating of ready/valid, and the final yield write. A false guard holds storage and disables both handshakes. Failure reports the original span and returns -1 without rollback (how/should/compiler/builders/fail.md).

tests/pipeline_lower_test.c and make pipeline-lower-test cover five anchors: N−1 BUF stage instances, per-stage next-value equations, yield destination provenance to the established packet, emitted member order with the first yielded field at the MSB, and guarded BUF handshake behavior. The member-order witness reads enclosing module input d in its first stage; reading an uninitialized pipeline field there is invalid. Negative witnesses include first-stage field reads, unequal adjacent packet widths and rejection of a real non-pipeline node.

This service owns the emitted member-order pin because the aggregate's width-keyed data type cannot distinguish concatenation order (does/the/concatenation/data/type/keying/keep/member/order.md). It also supplies behavioral witnesses for atomic external consuming reads and at most one item per cycle, beyond source-level stage-law checks. Its unit manifest establishes that service's contract; production and generated-context acceptance additionally require independent compiler/hardware tests.
