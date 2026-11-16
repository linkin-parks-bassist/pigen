---
status: green
revised_at: "2026-11-15T05:21:53+11:00"
checked_at: "2026-09-29T09:06:22+10:00"
---

One complete guard, every destination readiness, and every distinct consuming source validity determine one fire event. All buffered sources consume and all destinations update together or none do. Repeated references/projections of one base consume once. Constants and statics add no consumption; lvalue address/index reads participate when their laws consume.

This is an expression-wide dependency rule: `select ? a.o : d.o` selects one payload but consumes both referenced tokens on fire. It does not provide conditional token consumption. The child_port_expression_conditional regression checks this with producers at different rates and downstream backpressure. Use peek for observation without a consuming dependency; procedural transfer guards describe separate guarded transfers.

src/expression_use.c visits semantic expressions and collects symbol/signal uses, including predicates for conditional arms. src/resolve.c analyze_transfer combines those uses with destination and guard dependencies. Conditional analysis interns predicates and can relocate both predicate arenas: capture guard atom counts and identities, then reacquire records across analysis calls. Nested conditional native-record regressions in tests/native_protocol_test.py exercise this boundary.
