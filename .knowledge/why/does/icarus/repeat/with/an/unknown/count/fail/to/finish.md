---
status: green
revised_at: "2026-11-15T16:36:14+11:00"
---

The installed Icarus 13 fails to finish the checked native-SV unsigned-count reproducer within a two-second timeout. The generated Pigen program is not required to reproduce it:

```systemverilog
module probe;
  reg [7:0] v=8'b000000x1;
  integer c=0;
  initial begin
    repeat(v) begin c++; v=0; end
    $display("count %d",c);
    $finish;
  end
endmodule
```

Compiling with iverilog -g2012 and running under timeout 2 returns 124. A signed-count probe with xxxx0001 completes with zero iterations. The precise Icarus implementation cause has not been established; this evidence does not establish every unknown-count behavior or agreement with other simulators.

The continuous_intrinsics loop fixture uses defined two-state counts for its count-mutation witness and retains signed negative/zero counts and four-state while conditions. Typed repeat nodes preserve source state and emit native repeat; the compiler does not clear unknown counts or reinterpret their sign to accommodate this simulator. Unknown unsigned repeat counts remain outside that simulation witness. Recheck this standalone probe and an independent simulator before expanding that oracle coverage. Loop representation: how/are/combinational/loops/represented.md.
