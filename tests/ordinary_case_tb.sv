module ordinary_case_tb;
 logic clk=0; integer calls; logic [7:0] value;
 ordinary_case dut(.*);
 always #5 clk=~clk;
 initial begin
  #6;
  if(calls!==1 || value!==20) $fatal(1,"case selector evaluated %0d times, value %0d",calls,value);
  #10;
  if(calls!==2 || value!==20) $fatal(1,"second selector evaluation");
  $display("PASS ordinary case semantics"); $finish;
 end
endmodule
