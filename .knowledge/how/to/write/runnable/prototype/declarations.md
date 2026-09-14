---
status: "unverified"
created_at: "2026-09-14T20:35:24+10:00"
scope: "local"
source: "USER_GUIDE.md Start here, Pipelines, Choose the transfer type, Module boundaries, Fixed-point; retirement audit Codex /root 2026-09-14"
---

The unlinked production executable still accepts transfer-first prototype declarations: `buf [15:0] work;`, `fifo [31:0][16] requests;`, `skid [31:0] response;`, `port [31:0] bram_result;`. FIFO spelling puts payload range before depth; reversing them is invalid. Signed payloads use `buf signed [23:0] sample;` or `fifo signed [23:0][4] samples;`; emitted payload and primitive type parameters retain signedness. `$signed` is an intentional reinterpretation. These forms are quarantined executable behavior, never an alternate specified Pigen language or compatibility promise. Target declarations are data-first and must replace them when one complete structured production path is ready.

Prototype ANSI ports may use `input buf [31:0] request` and `output fifo [31:0][8] response`. Public expansion uses payload, `_valid`, `_ready`. Input buf/fifo/skid is an upstream-owned view, not duplicate child storage; local validity actions cannot reach upstream. Child output storage belongs to the child. Connect generated public ports with ordinary SV instantiation and explicit signals; never depend on private `__pigen_*` names.

An executable inline pipeline goes inside a clocked process: `pipeline add_then_clip begin logic [8:0] sum; logic [7:0] result; stage begin sum <= left + right; end stage begin result <= sum[8] ? 8'hff : sum[7:0]; end yield result; endpipeline`, then `out <= add_then_clip;`. This is an elastic packet transform, not an untimed multi-cycle program. Runnable examples/tests remain in examples/ and tests/.
