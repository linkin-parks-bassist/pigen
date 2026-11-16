---
status: green
revised_at: "2026-11-15T17:01:25+11:00"
---

The installed Icarus 13 has two independently reproduced frontend failures on native SV for headers. An empty initializer crashes compilation with exit 139:

```systemverilog
module probe(input logic value,output logic [7:0] result);
  integer i;
  always_comb begin
    result=0;i=0;
    for(;i<3;i=i+1) result=result+1;
  end
endmodule
```

A comma-separated step list rejects with exit 2 and Error in for loop step assignment:

```systemverilog
module probe(input logic v,output logic [7:0] result);
  integer i,j;
  always_comb begin
    result=0;j=3;
    for(i=0;i<3;i=i+1,j=j-1) result=result+8'(j);
  end
endmodule
```

Both reproduce with iverilog -g2012 -s probe, without Pigen. The precise simulator implementation causes remain unestablished.

Pigen keeps initializer, step and body as distinct FOR IR phases but renders an initializer followed by while(condition), body and ordered steps. This preserves supported semantics while avoiding these headers. Continue now disables a private iteration-body block before the step phase; break disables the whole loop (how/are/combinational/loop/exits/represented.md). No warning suppression or simultaneous concatenation assignment replaces sequential step dependencies. tests/cores/continuous_intrinsics* checks dependent comma steps and omitted header phases; tests/native_array_test.py checks all-family generated loop scope behavior. how/are/combinational/for/loops/represented.md owns the compiler model. Recheck the native probes before retiring this emission compatibility choice.
