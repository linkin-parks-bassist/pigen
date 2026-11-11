---
status: green
revised_at: "2026-11-11T16:09:32+11:00"
checked_at: "2026-09-29T09:06:22+10:00"
---

One complete guard, every destination readiness, and every distinct consuming source validity determine one fire event. All buffered sources consume and all destinations update together or none do. Repeated references/projections of one base consume once. Constants and statics add no consumption; lvalue address/index reads participate when their laws consume.

This is an expression-wide dependency rule: `select ? a.o : d.o` selects one payload but consumes both referenced tokens on fire. It does not provide conditional token consumption. The child_port_expression_conditional regression checks this with producers at different rates and downstream backpressure. Use peek for observation without a consuming dependency; procedural transfer guards describe separate guarded transfers.
