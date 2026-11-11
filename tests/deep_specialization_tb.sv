module deep_specialization_tb;
 logic clk=0, reset=1;
 logic [7:0] i=0, o;
 logic i_valid=0, i_ready, o_valid, o_ready=0;
 integer sent=0, received=0, cycle=0;
 specialization_top dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  i_valid=!reset && sent<30; i=sent;
  o_ready=cycle%5<3;
 end
 always @(posedge clk) if(!reset) begin
  if(o_valid && o_ready) begin
   if(o !== received[7:0]) $fatal(1,"deep hierarchy ordering");
   received=received+1;
  end
  if(i_valid && i_ready) sent=sent+1;
  if(cycle==100) begin
   if(received!=30) $fatal(1,"deep hierarchy stalled %0d",received);
   $display("PASS deep specialization"); $finish;
  end
 end
endmodule
