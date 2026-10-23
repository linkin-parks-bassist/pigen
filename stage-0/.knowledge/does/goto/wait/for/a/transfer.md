---
status: green
revised_at: "2026-09-13T15:07:13+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

No. goto has only its explicit enclosing guard; it never implicitly waits for handshake. Use accepts or an if(destination <= source) condition when a transition must occur on accepted transfer.
