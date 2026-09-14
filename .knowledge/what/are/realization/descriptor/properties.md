---
status: "unverified"
created_at: "2026-09-13T15:04:57+10:00"
scope: "local"
source: "Full 2026-08-24 realization design/plan; transfer_type owner; retirement audit 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:37:11+10:00"
---

The owner in transfer_type.c maps abstract->boundary, wire->combinational net, reg/logic->procedural variable, buf->elastic slot, port->pulse register, fifo->parameterized queue, skid->skid queue. Each source descriptor names exactly one backend-neutral realization. The realization descriptor has capacity_source (none/fixed/argument), fixed_capacity, ready_dependency (external/constant/downstream/occupancy), has_occupancy and reset (none/procedural/empty).

Boundary: no capacity, external readiness, no occupancy, no reset. Net: no capacity, constant readiness, no occupancy/reset. Variable: no transfer capacity, constant readiness, no occupancy, user-controlled reset. Elastic: fixed one, downstream-combinational ready, occupied, reset empty. Pulse: fixed one payload slot, constant ready, no occupancy-based backpressure, reset empty. FIFO: argument capacity, fixed_capacity=0, registered-occupancy ready, occupancy, reset empty. Skid: fixed two, registered-occupancy ready, occupancy, reset empty. Capacity is consumable transfer storage, not mere payload register bits. Negative source control constants are context-dependent.

Public pigen_transfer_realization_descriptor_get and pigen_transfer_realization_is_valid fail closed for invalid zero, negative/out-of-range identity or incomplete properties; fixed_capacity is positive exactly for fixed capacity. Transfer lookup rejects omitted/incomplete realization entries. Descriptor transfer-depth argument must agree exactly with argument-based capacity. Source laws retain spelling, parameter form, write/control/consumption/production/ownership/domain rules; realization properties do not duplicate them or contain SV names/generated identifiers/port syntax. Abstract boundary remains external; it is not a guessed local constant. The early design required specialization before realizing abstract internal storage; current module-input boundary laws remain authoritative. General semantic passes query laws, one lowerer boundary enumerates realizations.
