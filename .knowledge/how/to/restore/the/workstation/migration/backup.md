---
status: green
revised_at: "2026-10-24T10:40:17+11:00"
---

David authorizes publishing all Pigen work to the personal GitHub repository for workstation migration. This authorization covers Pigen branches and snapshots of its linked worktrees and stashes; it does not authorize publishing unrelated workstation or employer material.

Migration backups use `backup/migration-<UTC timestamp>/...` branches on origin (https://github.com/linkin-parks-bassist/pigen.git). Snapshot commits preserve worktree contents without changing the source worktree's branch, index or files. Separate staged-state snapshots preserve existing index contents; stash snapshots preserve each stash commit and its parents. The inventory backup branch carries MIGRATION_BACKUP.json mapping original checkout locations and heads to their snapshot refs. These are recovery snapshots, not reviewed implementation landings.

On the new workstation clone the repository and fetch its branches, then inspect the inventory branch for the backup timestamp. Create local branches or worktrees from the required backup refs. Normal Git clones do not recreate linked-worktree directories or the original stash stack automatically; recover their contents from the mapped snapshot branches. Ignored build outputs are excluded and can be regenerated. Remote commit-ID verification establishes uploaded snapshot identity, not build correctness or quiescence of concurrently running workers.
