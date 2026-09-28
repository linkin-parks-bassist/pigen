---
status: green
revised_at: "2026-09-13T15:07:21+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

A compilation layout interleaves opaque immutable written spans and structured module layouts. Nested module layouts order opaque items and RTL identity references. Coverage must be exact, monotonic and once per written byte, rejecting gaps/overlaps/reversed/wrong-source spans. Terminal emission copies opaque bytes exactly and renders structured items; layout is not semantics.

Related: [when can a module become structured](../../../../../../when/can/a/module/become/structured.md).
