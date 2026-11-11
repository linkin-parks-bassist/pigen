module child_ready_manual_tb;
 parameter CONSTANT_READY=0;
 logic clk=0, reset=1, offer=0, enable=0, ready=0, accepted, valid;
 logic [7:0] data=0,result;
 integer sent=0, received=0, cycle=0;
 manual_top dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  data=sent; offer=!reset && sent<20;
  enable=cycle%3!=0; ready=cycle%5<3;
 end
 always @(posedge clk) if(!reset) begin
  if(valid && (CONSTANT_READY || (enable && ready))) begin
   if(result!==received[7:0] || received>=sent) $fatal(1,"manual ready ordering %0d",result);
   received=received+1;
  end
  if(offer && accepted) sent=sent+1;
  if(cycle==150) begin
   if(received!=20) $fatal(1,"manual ready loss %0d",received);
   $display("PASS manual child ready"); $finish;
  end
 end
endmodule
