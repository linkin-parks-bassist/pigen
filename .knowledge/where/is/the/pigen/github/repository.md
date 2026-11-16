---
status: green
revised_at: "2026-11-17T07:41:21+11:00"
---

Pigen's personal GitHub repository is https://github.com/linkin-parks-bassist/pigen. Its origin fetch and push URLs are https://github.com/linkin-parks-bassist/pigen.git. The default branch master is the canonical published version, including the native AXI module-interface and fabric implementation selected by David. Working branches hold development snapshots.

Inspect git remote -v, git branch --show-current and the configured upstream before publishing. A working branch may have no upstream even when master tracks origin/master. Missing tracking configuration alone does not authorize publication or a tracking change.

Check remote refs before updating the canonical branch. Current user instructions govern history replacement, and an explicit expected-tip lease scopes an authorized forced update. A local master branch can be checked out in another worktree, so inspect git worktree list before moving local branches.

GitHub CLI's active account can differ from the repository owner. Inspect gh auth status if authentication fails. Repository and default-branch facts are verified through Git configuration and git ls-remote --symref origin HEAD.
