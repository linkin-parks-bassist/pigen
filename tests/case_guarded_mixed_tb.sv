module case_guarded_mixed_tb;
 logic clk=0; logic [1:0] sel=1; logic [7:0] i=10;
 logic ir; logic [7:0] o,temp; logic ov;
 always #5 clk=~clk;
 mixed_case dut(.clk(clk),.sel(sel),.i(i),.i_valid(1'b1),.i_ready(ir),
 .o(o),.o_valid(ov),.o_ready(1'b1),.temp(temp));
 initial begin
  #6;
  if(temp!==11 || !ov || o!==11) $fatal(1,"matching case branch");
  sel=0; i=20;
  #10;
  if(temp!==11 || !ov || o!==11) $fatal(1,"default retains prior value");
  sel=2'bx1;
  #10;
  if(temp!==11 || !ov || o!==11) $fatal(1,"unknown selector must take default, got %h",o);
  $display("PASS mixed case guards"); $finish;
 end
endmodule
