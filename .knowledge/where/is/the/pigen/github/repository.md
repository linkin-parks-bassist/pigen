---
status: green
revised_at: "2026-10-24T10:38:49+11:00"
---

Pigen's personal GitHub repository is https://github.com/linkin-parks-bassist/pigen. David identifies this as the intended personal repository. The shared repository's origin fetch and push URLs are https://github.com/linkin-parks-bassist/pigen.git; master is configured to track origin/master.

A local agent working branch can have no upstream even though origin and master's tracking configuration exist. Check `git remote -v`, `git branch --show-current`, and `git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}'` before interpreting an editor's missing-upstream message. Do not publish or change branch tracking merely to clear that message without appropriate user authorization. GitHub CLI's active account can differ from the repository owner; inspect `gh auth status` when authentication fails. These facts were verified using Git configuration, gh auth status and git ls-remote origin.
