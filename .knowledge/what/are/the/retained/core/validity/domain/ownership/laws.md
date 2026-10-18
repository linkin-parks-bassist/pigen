---
status: green
revised_at: "2026-10-18T14:03:44+11:00"
---

## What it is

The retained core's validity, domain and ownership laws for a single transfer. The route build enforces them against the signal-uses a transfer carries:

- **Validity, producer-owns**: local synchronous validity actions apply only to a locally owned buf/fifo/skid/port; a CONSUMER role on a non-locally-owned signal (an upstream INPUT/INOUT whose validity is owned by its producer) is diagnosed.
- **Domain, one-edge binding**: a domain is canonical by resolved clock-declaration identity and event edge; a signal whose transfer type binds a domain, first used in one Pigen domain, then used on a different edge of the same clock declaration (a multi-edge/cross-domain form) is diagnosed at the original span.
- **Ownership, self-consumption**: a signal carrying both the PRODUCER and CONSUMER role in the same transfer is diagnosed (a consuming transfer cannot read one of its own buffered destinations).
- **Ownership, exclusive**: a buffered source or destination (a type whose descriptor requires ownership) participates in a transfer only under its matching exclusive role.

## Status

Not implemented on the structured route. The route build currently enforces only module isolation, sizing/signedness/aggregate width, and ready-graph acyclicity; it accepts every VDO witness.

## Known defect in the discriminating manifest

The discriminating manifest for these laws is internally over-constrained and cannot be made green by any per-transfer law:

- **validity-source-order** builds two transfers, each `(dest=d, value=s, signal-uses=NULL)`, and requires the route to accept (`rc == 0`).
- **ownership-exclusive-required** builds one transfer of the identical per-transfer shape `(dest=d, value=s, signal-uses=NULL)` and requires the route to reject (`rc == -1`).

`d` is a locally owned INTERNAL buf and `s` an INPUT buf in both. The per-transfer model is byte-identical; only the transfer count differs. No per-transfer law can both accept the two-transfer unit and reject the one-transfer unit, so the manifest is unsatisfiable. A second tension: **validity-producer-owns** requires rejecting a CONSUMER role on an INPUT signal, while the registered route-law projection-dedup witness legitimately carries a CONSUMER role on an INPUT base token and requires acceptance.

## Why it matters

Until the manifest is corrected to be satisfiable (and mutually consistent with the registered route-law witnesses), the VDO implementation step cannot be completed: turning the manifest green would require either regressing the registered route-law projection-dedup test or skipping a section, both outside the step's scope.
