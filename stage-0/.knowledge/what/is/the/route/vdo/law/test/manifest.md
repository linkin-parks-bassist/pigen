---
status: green
revised_at: "2026-10-19T14:35:58+11:00"
---

tests/route_validity_domain_ownership_test.c is the retained-core route's test contract for the validity/domain/ownership laws (spec: what/is/the/spec.md). It is a six-section discriminating manifest wired as one `make route-vdo-test T=<section>` entry per section in tests/contracts.json (67 entries total); the Makefile target compiles the test with the exact CFLAGS (-std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude). It is satisfiable, law-consistent with the green route-law witness law-projection-dedup (tests/route_law_test.c), and every section independently discriminates its named law: a src/route.c that drops a law turns exactly that section red. No production code is touched by the contract.

The satisfiable law set is exactly three, constructible through the existing owner APIs (pigen_route_build/check over signal-use roles + domain binding) with no new public API (include/pigen/route.h exposes only init/build/check/free; there is no validity-action API):
1. value-CONSUMER: a buffered VALUE (the transfer's read source; a signal whose transfer type requires_ownership, i.e. buf/fifo/skid/port) requires the CONSUMER role; a transfer reading a buffered value without naming it CONSUMER is diagnosed. CONSUMER on an INPUT buf is legal (law-projection-dedup anchor) - only the MISSING role on a buffered value is diagnosed.
2. self-consumption: a signal may not carry both CONSUMER and PRODUCER in the same transfer (a consuming transfer cannot read one of its own buffered destinations).
3. multi-edge / domain one-edge binding: a signal whose transfer type binds a domain (binds_domain) is bound to one synchronous one-edge clock domain by its first Pigen use; a later use on a different clock edge is diagnosed (a whole-unit first-use-binding law, not a per-signal field comparison).

A "buffered DESTINATION requires PRODUCER" law is EXCLUDED: law-projection-dedup carries CONSUMER on its BUF value with NO PRODUCER on its BUF destinations and is rc==0, so a dest-PRODUCER law would reject the green route-law witness.

The six sections:
- signal-use-consumer-legal (rc==0): positive anchor pinning the legal pairing {s CONSUMER, d PRODUCER}, consistent with law-projection-dedup (anti-over-rejection).
- validity-source-order (rc==0): two transfers {s CONSUMER}, source-order composition, later dominates, node_count==2 in arena order.
- domain-one-edge-binding (rc==-1): multi-edge same-clock rejection; the rejection comes from the two-edge form, never CONSUMER-on-INPUT.
- domain-async-reset-diagnose (rc==0): one-edge synchronous EMPTY-reset form accepted.
- ownership-self-consumption (rc==-1): value = non-buffered LOGIC input x (requires_ownership false, so the value-CONSUMER law does NOT fire), dest = buffered slot d carrying CONSUMER|PRODUCER in the same transfer - the self-consumption form is the SOLE violation. (The earlier witness used a buffered INPUT value, which the value-CONSUMER law co-caught, so the section did not independently discriminate; the witness was corrected to the LOGIC read source.)
- ownership-exclusive-required (rc==-1): dest=d from value=s (buffered), uses={d PRODUCER} only; the buffered value s's CONSUMER role is MISSING, so the value-CONSUMER law fires; per-transfer shape {d PRODUCER} differs from validity-source-order's {s CONSUMER}.

Verified (Makefile exact CFLAGS): compiles clean; on present master src/route.c (no VDO law) 3 sections are green and 3 are discriminating-red, each failing REQUIRE(rc == -1) at the promised rejection after setup passes; a src/route.c implementing exactly the three laws passes all six sections and route-law-test stays 4/4 (law-projection-dedup, law-projection-complete, law-concat-order, law-coslice-order green). Independent discrimination: dropping self-consumption makes only ownership-self-consumption red (the witness is accepted by a value-CONSUMER-only route); dropping multi-edge makes only domain-one-edge-binding red; dropping value-CONSUMER makes only ownership-exclusive-required red.
