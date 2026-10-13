---
status: green
revised_at: "2026-10-13T22:31:55+11:00"
checked_at: "2026-09-30T10:17:35+10:00"
---

Task 1 establishes the RTL identity and arena foundations. include/pigen/ids.h defines eight distinct RTL IDs: type, expression, object, instance, equation, update, process and module. include/pigen/rtl.h and src/rtl.c own the eight pointer/count/capacity arena triples. Current records retain an origin span, allowing invalid provenance for synthetic hardware, and contain no source-text pointer. Checked accessors reject null models, invalid IDs and out-of-range indices; appends guard identity exhaustion and grow arena storage; destruction releases every arena and zeroes the model.

The origin-only constructors are foundations for the richer approved records in later tasks. Invalid-zero applies to enum kinds when introduced. tests/rtl_test.c covers empty-model rejection, distinct per-kind identities, synthetic origin, growth and out-of-range rejection.

Proof:

```bash
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -o /tmp/pigen-task1-check tests/rtl_test.c src/rtl.c src/source.c src/util.c && /tmp/pigen-task1-check >/tmp/pigen-task1-out 2>&1
```

Related: [what is elastic rtl task two](two.md).
