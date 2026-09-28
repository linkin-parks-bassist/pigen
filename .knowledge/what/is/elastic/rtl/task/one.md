---
status: brown
revised_at: "2026-09-14T23:16:10+10:00"
checked_at: '2026-09-14T17:13:16+10:00'
---

Task 1 is implemented and included in the authorized linear master publication. include/pigen/ids.h defines eight distinct RTL IDs: type, expression, object, instance, equation, update, process and module. include/pigen/rtl.h and src/rtl.c own the eight pointer/count/capacity arena triples. Current records retain an origin span, allowing invalid provenance for synthetic hardware, and contain no source-text pointer. Checked accessors reject null models, invalid IDs and out-of-range indices; appends guard identity exhaustion and grow arena storage; destruction releases every arena and zeroes the model.

The current origin-only constructors are foundations for the richer approved records in later tasks. Invalid-zero applies to enum kinds when introduced. tests/rtl_test.c covers empty-model rejection, distinct per-kind identities, synthetic origin, growth and out-of-range rejection. Task 2 is next.

Proof:

```bash
make rtl-test
```

Related: [what is elastic rtl task two](two.md), [what is the state](../../../the/state.md).
