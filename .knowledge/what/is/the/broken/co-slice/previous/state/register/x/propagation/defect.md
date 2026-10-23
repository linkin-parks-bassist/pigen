---
status: green
revised_at: "2026-10-16T03:00:20+11:00"
---

A co-sliced transfer whose members include an uninitialized, never-reset state register read by `previous <= state` in a separate always_ff clause emits `previous` as X when the testbench drives the source valid for a single clock edge and samples `previous` in that same cycle.

Mechanism: the state register (e.g. `reg [15:0] state;`, no initial value, no reset) holds X before the first active edge. The co-slice aggregate writes `state` on that edge, but the separate non-blocking `previous <= state` clause captures `state`'s pre-edge value — X — in the same cycle. A later cycle would hold the correct value; single-edge stimulus exposes the X. This is independent of co-slice destination width resolution and is not fixed by the typedef-width fix (e9dadfd).

Witness: `tests/coslice_xprop.pigen` / `tests/coslice_xprop_tb.sv` (`make coslice-xprop-test`) — a minimal literal-width [7:0] aggregate `{left, state} <= {source + 8'd1, peek(source)}` with `state` uninitialized and a separate `previous <= state` clause; the testbench drives `source_valid` for one edge and asserts `previous === 8'h3c`. Red on master ("previous X-propagation under single-edge stimulus: got xx expected 3c", exit 2). Its own red test-contract is the accepted packet d509b5d; a matching implementation item turns it green without touching the width witness.

The bundled width witness that originally carried this assertion (b5cdd90's `coslice-typedef-test` line 19) has been corrected to a pure typedef-width witness so the width fix and the X-propagation fix each own exactly one mechanism.
