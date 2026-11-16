---
status: green
revised_at: "2026-11-17T10:16:40+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured frontend is already the production route. Its pipeline support remains partial: repeated-field stage updates reject, as documented in how/do/pipeline/stage/reads/work.md. The completed frontend switch does not establish complete language support; what/is/broken.md owns capability gaps.

The migration condition required the structured route to cover retained production features and remove corresponding textual parsers/scanners/emitters in the same change. The production architecture uses one frontend without textual preprocessing into a second dialect, per-file old/new selection or an extra validator.
