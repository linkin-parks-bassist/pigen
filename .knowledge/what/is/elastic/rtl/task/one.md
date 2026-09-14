---
status: "unverified"
created_at: "2026-09-13T15:05:46+10:00"
scope: "local"
source: "David's current-only knowledge instruction 2026-09-14; audited current contracts and inspected owner interfaces; Codex /root"
ingested_by: "Codex /root"
checked_at: "2026-09-14T17:13:16+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:48:46+10:00"
updated_by: "opencode"
---

Task 1 is implemented and awaiting review in PR 1. include/pigen/ids.h defines eight distinct RTL IDs: type, expression, object, instance, equation, update, process and module. include/pigen/rtl.h and src/rtl.c own the eight pointer/count/capacity arena triples. Current records retain an origin span, allowing invalid provenance for synthetic hardware, and contain no source-text pointer. Checked accessors reject null models, invalid IDs and out-of-range indices; appends guard identity exhaustion and grow arena storage; destruction releases every arena and zeroes the model.

The current origin-only constructors are foundations for the richer approved records in later tasks. Invalid-zero applies to enum kinds when introduced. tests/rtl_test.c covers empty-model rejection, distinct per-kind identities, synthetic origin, growth and out-of-range rejection. Task 2 is next.

Proof: (verified at 2026-09-14T20:51:13+10:00)

```bash
make rtl-test
```

Related: [what is elastic rtl task two](two.md), [what is the state](../../../the/state.md).
