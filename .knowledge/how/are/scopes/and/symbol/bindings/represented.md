---
status: "unverified"
created_at: "2026-09-14T20:39:58+10:00"
scope: "local"
source: "SPEC/PLAN/architecture/semantic invariants/Ari/editorial notes linear retirement audit; David knowledge-tree-only instruction 2026-09-14"
---

Every declaration belongs to one scope and introduces one stable typed symbol identity. Runtime declarations all denote signals, including ordinary nets/variables. Each semantic symbol has one typed binding back to its object and that object retains the same symbol ID, permitting constant-time recovery without scanning names/object arenas. Identifier syntax is unresolved or refers to exactly one symbol; resolved-name-only states are forbidden. Scope parentage encodes lookup, including stage-local->pipeline-local->module. Duplicate declarations and shadowing diagnose at introducing spans. Generated names are allocated after semantic analysis and cannot affect lookup. Type structural equality and semantic identity never depend on spelling; general passes consume predecessor records, parse source constructs once, and never inspect opaque text for hidden dependencies.
