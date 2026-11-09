---
status: green
revised_at: "2026-11-10T09:10:30+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring with direct links and skid-buffered routed links, and nine migrated examples (buf, compiler, guarded, output, port, skid, fifo, join, fixed_point_mac). semantic-test is green.

Remaining work, in order:
1. Complete fabric routing: balanced three-port router networks, buffered hops, round-robin arbitration for multiple routed sources per destination, and SVG from the same topology.
2. Implement the validity actions `validate`, `invalidate` and `flush`; migrate the remaining examples (`text`, `biquad_bank`, `df1_biquad_bandpass`) with the required aggregate/action forms.
3. Rebaseline the nine rtl-lower and two sv-emit stale sections.
4. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
