module child_valid_manual_tb;
 parameter CONSTANT_VALID=0, DIRECT_VALID=0;
 logic clk=0, reset=1, offer=0, enable=0, ready=0, accepted, valid;
 logic [7:0] data=0,result;
 integer sent=0, received=0, cycle=0;
 logic [7:0] expected[0:255];
 wire [1:0] controls={1'b0,offer && enable};
 manual_top dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  data=sent; offer=!reset && sent<20;
  enable=DIRECT_VALID || cycle%3!=0; ready=cycle%5<3;
 end
 always @(posedge clk) if(!reset) begin
  if(valid && ready) begin
   if(received>=sent || result!==expected[received]) $fatal(1,"manual ready ordering %0d",result);
   received=received+1;
  end
  if((CONSTANT_VALID || (offer && enable)) && accepted) begin
   expected[sent]=dut.input_data; sent=sent+1;
  end
  if(cycle==150) begin
   if((!CONSTANT_VALID && received!=20) || (CONSTANT_VALID && (received<20 || sent-received>1))) $fatal(1,"manual ready loss %0d",received);
   $display("PASS manual child valid"); $finish;
  end
 end
endmodule
