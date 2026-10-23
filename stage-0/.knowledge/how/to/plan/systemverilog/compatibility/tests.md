---
status: green
revised_at: "2026-10-07T08:29:41+11:00"
checked_at: "2026-10-04T16:19:56+11:00"
---

Build a representative pure-SV corpus across design units, interfaces/packages/classes/generate/procedural/sizing/signedness/timing/assertions/arrays/preprocessing and verify it against the SV compatibility contract: ports, values, widths, signedness, events, reset and observable cycles are preserved, and Pigen does not reinterpret unrelated SV. Add mixed cases for the same. Audit each rejection as a specified deliberate restriction or an accidental bug. Acceptance is against the spec, not prototype behaviour. These broader compatibility gates remain incomplete.
