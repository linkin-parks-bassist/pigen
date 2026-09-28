---
status: green
revised_at: "2026-09-13T15:07:13+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Yes: identity is index-based and source_add owns immutable copies, so caller text mutation and later file additions do not change the stored source. Borrowed record pointers must be reacquired after arena growth rather than treated as stable pointers. source_test mutates the caller buffer, adds 32 files, reacquires first source and checks original bytes/locations including empty and EOF spans.
