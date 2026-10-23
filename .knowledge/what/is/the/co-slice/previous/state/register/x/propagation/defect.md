---
status: green
revised_at: "2026-10-15T23:13:15+11:00"
---

A co-sliced transfer whose members include an uninitialized, never-reset state register read by `previous <= state` in a separate always_ff clause emits `previous` as X when the testbench drives the source valid for a single clock edge and samples `previous` in that same cycle.

Mechanism: the state register (e.g. `reg [15:0] state;`, no initial value, no reset) holds X before the first active edge. The co-slice aggregate writes `state` on that edge, but the non-blocking `previous <= state` clause captures `state`'s pre-edge value — X — in the same cycle. A later cycle would hold the correct value (the literal-width analog `tests/coslice_tb.sv` avoids the trap by stalling the transfer a second cycle with `right_ready` held 0, letting `state` latch before `previous` is sampled).

This is a distinct mechanism from co-slice destination width resolution (what/is/broken.md). The typedef-width fix (non-literal widths sized via `$bits(destination)`) does not resolve it: `examples/coslice_typedef.pigen`'s `previous` still reads X under single-edge stimulus regardless of the width fix. It is tracked as its own defect with its own red witness and contract, separate from the typedef-width witness, so each fix owns exactly one witness.
