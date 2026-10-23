---
status: green
revised_at: "2026-09-14T17:30:00+10:00"
checked_at: '2026-09-14T17:30:00+10:00'
---

Yes: src/rtl.c and include/pigen/rtl.h exist in this checkout since commit 30ac091 (elastic RTL Task 1, pull request 1). This narrow presence fact does not by itself prove every structured-backend status assertion; the ready/output/emitter/composer interfaces remain absent. Run the predicate from repository root.

Proof:

```bash
test -e src/rtl.c && test -e include/pigen/rtl.h
```
