module child_input_alias_tb;
 logic clk=0, reset=1;
 wire [15:0] data; wire [1:0] valid;
 logic [1:0] ready=0;
 integer cycle=0, head[0:1], tail[0:1];
 logic [7:0] queue[0:1][0:255];
 slice_top dut(.*);
 always #5 clk=~clk;
 initial begin head[0]=0; head[1]=0; tail[0]=0; tail[1]=0; end
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  ready[0]=(cycle%4==0);
  ready[1]=(cycle%5!=0 && cycle%5!=1);
 end
 always @(posedge clk) if(!reset) begin
  for(integer lane=0; lane<2; lane=lane+1) begin
   if(valid[lane] && ready[lane]) begin
    if(head[lane]>=tail[lane] || data[lane*8 +: 8]!==queue[lane][head[lane]])
     $fatal(1,"alias ordering lane %0d at %0d",lane,head[lane]);
    head[lane]=head[lane]+1;
   end
  end
  if(dut.a.o_valid && dut.a.o_ready) begin queue[0][tail[0]]=dut.a.o; tail[0]=tail[0]+1; end
  if(dut.c.o_valid && dut.c.o_ready) begin queue[1][tail[1]]=dut.c.o; tail[1]=tail[1]+1; end
  if(cycle==100) begin
   if(head[0]<20 || head[1]<40) $fatal(1,"no progress");
   $display("PASS child input alias ownership"); $finish;
  end
 end
endmodule
