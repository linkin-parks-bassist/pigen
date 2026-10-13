---
status: green
revised_at: "2026-10-12T09:19:14+11:00"
checked_at: "2026-10-01T18:31:08+10:00"
---

Pipeline verification no longer fails on Icarus: with Icarus Verilog 13.0 installed at `~/.local/bin` (source build of tag v13_0, ahead of the Ubuntu 12.0 package `/usr/bin/iverilog` on PATH), make pipeline-test, pipeline-scope-test and biquad-bank-test pass unchanged. Verilator 5.020 is installed. Full verification and nonfatal lint-warning limits belong to what/is/broken.md; Icarus is no longer the blocker.

Why the Ubuntu 12.0 package fails, so it is not used again: Icarus 12 does not evaluate `$bits(<signal>)` in a declaration range. The prototype emits ingress endpoints that lower to `logic [($bits(source))-1:0] name__pigen_packet_in;` plus a continuous assign; that form segfaults ivl in elaboration (NetNet::test_and_set_part_driver, exit 139), and every alternative spelling (`wire`, a localparam, always_comb) silently simulates z/x. Only `$bits(<typedef>)` works. Minimal reproducer:

```systemverilog
module m(input logic [7:0] s); logic [$bits(s)-1:0] p; assign p = s; endmodule
```

The generated SV is legal. Icarus 13.0 handles the reproducer and all variants correctly.
