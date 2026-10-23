---
status: green
revised_at: "2026-10-03T20:38:23+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A compiler stage either works correctly or fails loudly: on error it reports a diagnostic and the compile stops. Builders do not need undo/rollback machinery for partially built state; a failed call may leave partial arenas, memos or records, and nothing uses the model after a failed compile, so no test asserts that a failed call left state unchanged. The exception is genuine control flow that continues after backing out, such as speculative syntax parsing (how/to/roll/back/speculative/syntax/construction.md). Syntax/semantic/graph/lowering errors use owning provenance; emitter failures are only malformed IR, allocation and output I/O.
