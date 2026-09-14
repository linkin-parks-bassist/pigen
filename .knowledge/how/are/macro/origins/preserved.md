---
status: "unverified"
created_at: "2026-09-13T15:03:16+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:15+10:00"
---

Fixed replacement tokens retain invocation and definition-token origins. Substituted arguments additionally retain formal-parameter and actual-token origins, including nested expansions. Continuation markers do not become replacement tokens; definitions retain physical spans covering continued lines. Expanded tokens are not rendered back into source to reconstruct meaning.
