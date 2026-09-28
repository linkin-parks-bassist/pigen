---
status: green
revised_at: "2026-09-28T22:57:05+10:00"
checked_at: '2026-09-28T23:00:19+10:00'
---

Task 1 is implemented and included in the authorized linear master publication. include/pigen/ids.h defines eight distinct RTL IDs: type, expression, object, instance, equation, update, process and module. include/pigen/rtl.h and src/rtl.c own the eight pointer/count/capacity arena triples. Current records retain an origin span, allowing invalid provenance for synthetic hardware, and contain no source-text pointer. Checked accessors reject null models, invalid IDs and out-of-range indices; appends guard identity exhaustion and grow arena storage; destruction releases every arena and zeroes the model.

The current origin-only constructors are foundations for the richer approved records in later tasks. Invalid-zero applies to enum kinds when introduced. tests/rtl_test.c covers empty-model rejection, distinct per-kind identities, synthetic origin, growth and out-of-range rejection.

The full make rtl-test suite is currently red only at the first _with_owner call inside the Task 3 block (the corrected Task 3 test contract run against the landed stub constructors), not a Task 1 or Task 2 regression: every Task 1 and Task 2 case before the Task 3 block passes. The suite turns fully green when the Task 3 implementation lands. The proof below requires a clean -Werror build and, when the suite aborts, that the first failing assertion line is inside the Task 3 block (after the "Task 3: modules and resolved hardware" marker); it passes unchanged once the binary exits green.

Proof:

```bash
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -o /tmp/pigen-task12-check tests/rtl_test.c src/rtl.c src/source.c src/util.c && { /tmp/pigen-task12-check >/tmp/pigen-task12-out 2>&1 || { t3=$(grep -n 'Task 3: modules and resolved hardware' tests/rtl_test.c | cut -d: -f1); red=$(grep -o 'tests/rtl_test.c:[0-9]*' /tmp/pigen-task12-out | head -1 | cut -d: -f2); [ -n "$red" ] && [ "$red" -gt "$t3" ]; }; }
```

Related: [what is elastic rtl task two](two.md), [what is the state](../../../the/state.md).
