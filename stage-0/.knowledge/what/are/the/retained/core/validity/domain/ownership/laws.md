---
status: green
revised_at: "2026-10-19T21:32:21+11:00"
---

## What it is

The retained core's validity, domain and ownership laws for a single transfer. The route build enforces them against the signal-uses a transfer carries and the unit's clock-domain bindings:

- **Validity, producer-owns**: a CONSUMER role on an upstream INPUT buf is legal (a transfer reads a buffered input as its consuming source); the route accepts it.
- **Domain, one-edge binding**: a domain is canonical by resolved clock-declaration identity and event edge; a signal whose transfer type binds a domain, first used in one Pigen domain, then used on a different edge of the same clock declaration (a multi-edge/cross-domain form) is diagnosed at the original span.
- **Domain, asynchronous reset**: a transfer whose destination's reset binding is not the synchronous EMPTY form (the asynchronous-reset form) is diagnosed.
- **Ownership, self-consumption**: a signal carrying both the PRODUCER and CONSUMER role in the same transfer is diagnosed (a consuming transfer cannot read one of its own buffered destinations).

## Ownership, value-CONSUMER: not enforceable at the route's contract level

The spec's "exclusive buffered producers/consumers" obligation, read as "a buffered VALUE read source (a requires_ownership signal) must carry the CONSUMER role in the transfer that reads it," is NOT enforceable by the route's build/check at the contract level. The protected route witnesses (tests/route_test.c, route-shape and route-check) build a unit whose value is a BUF signal read with an EMPTY signal-use set and require the route to accept it (rc == 0); a route that rejects that form fails both witnesses. The route has no per-signal consumer/producer aggregation, so it cannot distinguish the EMPTY-use legal unit from a buffered-value-missing-CONSUMER unit on any observable input. A per-transfer "buffered value not named CONSUMER" check that is gated on the transfer naming some signal-use (signal_use_count > 0) is a surface/shape rule, not the spec law: a route keyed on unit process count, on a use carrying CONSUMER|PRODUCER, and on a buffered value absent from a NON-EMPTY use set passes every route-level ownership witness without enforcing the law. The only route-enforceable ownership law is self-consumption, which the route pins with its own independent witness. The buffered DESTINATION-requires-PRODUCER form is likewise not a route law: it would reject the green law-projection-dedup witness (a buffered value read source named CONSUMER whose buffered destinations carry no PRODUCER role), so it is excluded.

## Status

Implemented on the structured route. pigen_route_build enforces, per transfer, the self-consumption law and the legal CONSUMER-on-INPUT-buf anchor, plus a whole-unit cross-transfer domain sweep (one-edge binding and the asynchronous-reset form); each violation reports -1 with check_valid = 0 and anchors check_span at the original span. The green manifest (tests/route_validity_domain_ownership_test.c, one make route-vdo-test entry per section) is green.
