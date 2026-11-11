module child_port_unused_tb;
 logic clk=0, reset=1, external_ready=1;
 endpoint_top dut(.*);
 always #5 clk=~clk;
 initial begin
  repeat(3) @(negedge clk);
  reset=0;
  repeat(10) begin
   @(negedge clk);
   if(dut.b.i_valid !== 1'b0 || dut.b.i !== 8'd0) $fatal(1,"unused input must be inactive");
   if(dut.a.o_ready !== 1'b0 || dut.b.o_ready !== 1'b0 || dut.c.o_ready !== 1'b0)
    $fatal(1,"unused output must not consume");
   if(dut.d.o_ready !== external_ready) $fatal(1,"explicit readiness was overridden");
   external_ready=~external_ready;
   if(dut.b.o_valid !== 1'b0) $fatal(1,"unused input produced a token");
  end
  $display("PASS unused child endpoints"); $finish;
 end
endmodule
