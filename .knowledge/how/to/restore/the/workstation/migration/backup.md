---
status: green
revised_at: "2026-10-24T10:41:28+11:00"
---

Pigen's workstation recovery inventory is on GitHub at branch `backup/migration-20261023T234051Z/inventory` in https://github.com/linkin-parks-bassist/pigen. Its MIGRATION_BACKUP.json maps 51 registered worktrees and nine stash entries to recovery refs; 16 separate index snapshots preserve staged states. All 194 local branch refs at publication were verified against GitHub's commit IDs. Local worktree HEADs were unchanged during the backup.

Clone the personal repository on the new workstation and fetch its branches. Inspect the inventory branch, then create local branches or worktrees from the required `backup/migration-20261023T234051Z/worktree/...` refs. Normal clones do not recreate original worktree directories or the stash stack automatically. Stash branches retain stash commits and parents, so their contents can be recovered without relying on the old workstation.

Index snapshots are under the same prefix's `index/...` refs. For an unresolved merge, the snapshot archives entries under stage-0, stage-1, stage-2 and stage-3 paths, preserving conflict versions; the worktree snapshot retains conflict-marker file contents. Ordinary staged snapshots contain the original index tree.

These recovery commits preserve unfinished work, not reviewed implementation landings. Source checkout branches, files and indexes remain in place. Ignored build outputs are excluded. Remote commit-ID agreement verifies uploaded snapshot identity, not build correctness or quiescence of concurrently running workers.

David authorizes publishing Pigen work for migration, including branches, linked-worktree snapshots and stashes. This does not authorize publishing unrelated workstation or employer material.
