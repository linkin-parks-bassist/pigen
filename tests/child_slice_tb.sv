module child_slice_tb;
 logic clk=0, reset=1;
 wire [15:0] data; wire [1:0] valid;
 logic [1:0] ready=0;
 integer cycle=0, count=0, received[0:1];
 logic [1:0] expected_valid=0;
 logic [7:0] expected[0:1];
 slice_top dut(.*);
 always #5 clk=~clk;
 initial begin received[0]=0; received[1]=0; end
 always @(negedge clk) begin
  cycle=cycle+1;
  reset=cycle<3;
  ready[0]=(cycle%4==0);
  ready[1]=(cycle%5!=0 && cycle%5!=1);
 end
 always @(posedge clk) begin
  if(reset) begin count=0; expected_valid=0; end
  else begin
   for(integer lane=0; lane<2; lane=lane+1) begin
    if(valid[lane]!==expected_valid[lane]) $fatal(1,"valid lane %0d",lane);
    if(valid[lane] && data[lane*8 +: 8]!==expected[lane])
     $fatal(1,"payload lane %0d got %0d expected %0d",lane,data[lane*8 +: 8],expected[lane]);
    if(valid[lane] && ready[lane]) received[lane]=received[lane]+1;
    if(!expected_valid[lane] || ready[lane]) begin
     expected[lane]=count[7:0]; expected_valid[lane]=1;
    end
   end
   count=count+1;
   if(cycle==100) begin
    if(received[0]<20 || received[1]<40) $fatal(1,"no progress");
    $display("PASS child slice ownership"); $finish;
   end
  end
 end
endmodule
