module wide_literals_tb;
 logic clk=0, reset=1, payload_ready=0, shifted_ready=0;
 logic [192:0] payload;
 logic signed [127:0] shifted;
 logic payload_valid, shifted_valid;
 integer cycle=0, received=0;
 wide_literals dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  payload_ready=cycle%3!=0; shifted_ready=cycle%5!=0;
 end
 always @(posedge clk) if(!reset) begin
  if(payload_valid && payload_ready) begin
   if(payload!==193'bz1001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110011001100110xz) $fatal(1,"wide four-state literal changed");
   received=received+1;
  end
  if(shifted_valid && shifted_ready &&
   shifted!==(128'sh80000000000000000000000000000000 >>> 4))
   $fatal(1,"wide signed literal shift changed");
  if(cycle==50) begin
   if(received<20) $fatal(1,"insufficient progress");
   $display("PASS wide literals"); $finish;
  end
 end
endmodule
