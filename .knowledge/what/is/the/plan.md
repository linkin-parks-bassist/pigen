---
status: green
revised_at: "2026-11-09T15:44:11+11:00"
---

Remaining approved subsystem work:

1. Build child-instance and fabric support on the structured path, each in its own bounded stage with a discriminating green test manifest. The gates are owned by what/are/the/remaining/architecture/migration/gates.md; the language contract is what/is/the/spec.md. The prototype has no authority as a behavioral reference. No prototype-parity acceptance, validator bridge, fallback, second dialect or per-file choice is approved.

2. Complete the remaining frontend forms required by accepted constructs, including parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments, under those migration gates. Diagnose unsupported dependencies at original spans.

Baseline hardware experiment: David requested an isolated project with a copied compiled compiler and a Pigen-native counter to blink a Tang Nano 20K LED. The experiment is /home/david/Projects/pigen-experiments. Its counter is blocked by source-span overlap and missing clocked-process output mapping, detailed in why/does/the/native/counter/fail/sv/emission.md. Compilation, behavioral simulation and hardware build/programming are not complete. This experiment exposes a CLI acceptance gap; focused subsystem completion does not establish end-to-end counter behavior. A CointOS automatic repair cycle is only a discussion proposal, not an approved implementation task.

Deferred to Pigen 1.0 with ready-cycle validation (elastic RTL Task 7): three route contract-level negative witnesses (cycle/self-loop rejection, sizing/signedness/aggregate-law violation, non-isolated foreign-module transfer rejection), none constructible through the public owner APIs. The concatenated/co-slice member-order law remains unobservable through the public API and is pinned per path by emitted SV and golden witnesses (does/the/concatenation/data/type/keying/keep/member/order.md).

Open questions where the spec is silent on prototype-only behavior, for David to decide rather than inherit: whether pipeline yield is the complete pipeline control surface; whether fabric manifest/SVG outputs are retained; whether prototype-only examples/tests/APIs are preserved or retired when textual machinery is deleted. Struct-like child-instantiation grammar remains unsettled (is/struct/like/child/instantiation/specified.md).

Pipeline and FSM subsystem completion evidence belongs in what/is/the/pipeline/parent/module/lowering.md and what/is/the/fsm/parent/module/lowering.md, including their limits on production integration. Elastic RTL stage/manifest completion belongs in the task owners; CLI wiring is owned by does/the/structured/frontend/run/in/production.md. Current behavioral defects remain owned by what/is/broken.md.

Lowering consumes validated identities and backend-neutral realization properties. Stages diagnose and stop without rollback. Ordinary branches and worktrees may isolate bounded work; neither linear history nor direct-to-master work is required.
