module case_signed_tb;
 logic clk=0; logic signed [3:0] sel=15; logic [7:0] o,temp; logic ov,ir;
 case_signed dut(.clk(clk),.sel(sel),.i(8'd0),.i_valid(1'b1),.i_ready(ir),.o(o),.o_valid(ov),.o_ready(1'b1),.temp(temp));
 always #5 clk=~clk;
 initial begin
  #6;
  if(temp!==21 || !ov || o!==21) $fatal(1,"common case signedness: temp=%0d o=%0d",temp,o);
  sel=0;
  #10;
  if(temp!==31 || !ov || o!==31) $fatal(1,"default branch");
  $display("PASS common case signedness"); $finish;
 end
endmodule
