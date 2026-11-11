module exact_wide_tb;
 logic clk=0,reset=1,o_ready=1,o_valid;
 logic [193:0] o;
 integer received=0;
 exact_wide dut(.*);
 always #5 clk=~clk;
 initial begin
  #20 reset=0;
  repeat(10) begin
   @(negedge clk);
   if(o_valid) received=received+1;
   if(o_valid && o!==194'h2000000000000000000000000000000000000000000000001) $fatal(1,"exact wide value changed");
  end
  if(received<5) $fatal(1,"insufficient progress");
  $display("PASS exact wide"); $finish;
 end
endmodule
