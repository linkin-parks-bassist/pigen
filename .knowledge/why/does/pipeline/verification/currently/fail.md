---
status: green
revised_at: "2026-10-01T18:18:41+10:00"
checked_at: "2026-09-14T20:43:49+10:00"
---

make verify passes all eleven structured C foundation targets and the production smoke/fabric/core checks, then make pipeline-test fails under Icarus Verilog 12.0 (Ubuntu 12.0-2build2). Root cause (isolated 2026-10-01): Icarus 12 does not evaluate `$bits(<signal>)` correctly in a declaration range. The prototype emits pipeline ingress endpoints as `ingress [($bits(source))-1:0] name;`, which declarations.c lowers to `logic [($bits(source))-1:0] name__pigen_packet_in;` driven by a continuous assign. That exact form segfaults ivl in elaboration (NetNet::test_and_set_part_driver, exit 139); every alternative spelling that sizes from a signal (`wire`, a localparam W=$bits(s), always_comb, $size/$left) compiles but silently simulates as z/x or fails to bind. Only `$bits(<typedef>)` sizes correctly. Minimal reproducer:

```systemverilog
module m(input logic [7:0] s); logic [$bits(s)-1:0] p; assign p = s; endmodule
```

The generated SV is legal; pigen is not at fault, and the handshake logic was observed correct (valid/ready, stall and token counts) with only the payload lost. Declaring packet_in as a net removes the crash but exposes the silent mis-sizing (payload x, tests/pipeline_tb.sv fatal "reordered/lost payload 0"), so it was not kept. Fixing it inside Icarus's limits would require the textual prototype to resolve ingress expression types into typedefs, which is structured-frontend work. The chosen direction is to run hardware simulation under Verilator instead (5.020 is the Ubuntu candidate; not yet installed).
