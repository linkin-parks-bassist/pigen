---
status: green
revised_at: "2026-09-28T23:25:07+10:00"
checked_at: "2026-09-28T23:00:19+10:00"
---

Task 1 is implemented and included in the authorized linear master publication. include/pigen/ids.h defines eight distinct RTL IDs: type, expression, object, instance, equation, update, process and module. include/pigen/rtl.h and src/rtl.c own the eight pointer/count/capacity arena triples. Current records retain an origin span, allowing invalid provenance for synthetic hardware, and contain no source-text pointer. Checked accessors reject null models, invalid IDs and out-of-range indices; appends guard identity exhaustion and grow arena storage; destruction releases every arena and zeroes the model.

The origin-only constructors are foundations for the richer approved records in later tasks. Invalid-zero applies to enum kinds when introduced. tests/rtl_test.c covers empty-model rejection, distinct per-kind identities, synthetic origin, growth and out-of-range rejection.

The full make rtl-test suite is green (both PASS lines): every Task 1, Task 2 and Task 3 case passes now that the Task 3 implementation is landed. The proof below requires a clean -Werror build and a green suite run.

Proof:

```bash
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -o /tmp/pigen-task12-check tests/rtl_test.c src/rtl.c src/source.c src/util.c && /tmp/pigen-task12-check >/tmp/pigen-task12-out 2>&1
```

Related: [what is elastic rtl task two](two.md), [what is the state](../../../the/state.md).
