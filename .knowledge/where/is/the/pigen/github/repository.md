---
status: green
revised_at: "2026-11-21T23:20:24+11:00"
---

Pigen's personal GitHub repository is https://github.com/linkin-parks-bassist/pigen. Its origin fetch and push URLs are https://github.com/linkin-parks-bassist/pigen.git. The default branch master is the canonical published version, including the native AXI module-interface and fabric implementation selected by David. Working branches hold development snapshots.

Inspect git remote -v, git branch --show-current and the configured upstream before publishing. A working branch may have no upstream even when master tracks origin/master. Missing tracking configuration alone does not authorize publication or a tracking change.

Check remote refs before updating the canonical branch. Current user instructions govern history replacement, and an explicit expected-tip lease scopes an authorized forced update. A local master branch can be checked out in another worktree, so inspect git worktree list before moving local branches.

GitHub CLI's active account can differ from the repository owner, and on halomit `gh auth status` can report no hosts while per-user tokens still resolve. Verify a scoped token with `GH_TOKEN="$(gh auth token --user linkin-parks-bassist)" gh api user --jq .login`; the scoped credential procedure is global:how/to/push/the/knowledgetrees/repository/when/github/selects/the/wrong/account.md. Repository and default-branch facts are verified through Git configuration and git ls-remote --symref origin HEAD.
