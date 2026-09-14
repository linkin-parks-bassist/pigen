---
status: "unverified"
created_at: "2026-09-14T23:01:52+10:00"
scope: "local"
source: "Codex /root; observed Git push outputs and commit identities during Task 2 fixes"
---

When an amended local commit replaces an already-pushed commit, a normal push is rejected as non-fast-forward. If the pushed commit is the known exact predecessor and the working tree contains only the intended follow-up edits, preserve it: move the local branch back to that exact commit with git reset --soft COMMIT, then commit the retained difference as a new follow-up and push normally. Inspect status and commit identities first; do not use force-push. Observed here: 3af0150 had already pushed when its local amendment e2ea4b3 was rejected; retained corrections were committed on top of 3af0150.
