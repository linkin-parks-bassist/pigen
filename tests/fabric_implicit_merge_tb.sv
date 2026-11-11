module fabric_implicit_merge_tb;
 parameter SOURCES=2;
 logic clk=0, reset=1;
 logic [7:0] s0=0,s1=64,s2=128,result;
 logic s0_valid=0,s1_valid=0,s2_valid=0,s0_ready,s1_ready,s2_ready,result_valid,result_ready=0;
 integer sent[0:2], received[0:2], cycle=0, source;
 implicit_merge_top dut(.*);
 always #5 clk=~clk;
 initial for(integer n=0;n<3;n=n+1) begin sent[n]=0; received[n]=0; end
 always @(negedge clk) begin
  cycle=cycle+1; reset=cycle<3;
  s0_valid=!reset && sent[0]<20; s0=sent[0];
  s1_valid=!reset && sent[1]<20; s1=64+sent[1];
  s2_valid=!reset && SOURCES==3 && sent[2]<20; s2=128+sent[2];
  result_ready=cycle%5<3;
 end
 always @(posedge clk) if(!reset) begin
  if(result_valid && result_ready) begin
   source=result[7:6];
   if(source>=SOURCES || result[5:0]!==received[source][5:0]) $fatal(1,"merge order or duplicate %h",result);
   received[source]=received[source]+1;
  end
  if(s0_valid && s0_ready) sent[0]=sent[0]+1;
  if(s1_valid && s1_ready) sent[1]=sent[1]+1;
  if(s2_valid && s2_ready) sent[2]=sent[2]+1;
  if(cycle==150) begin
   for(integer n=0;n<SOURCES;n=n+1) if(received[n]!=20) $fatal(1,"merge loss/starvation source %0d count %0d",n,received[n]);
   $display("PASS implicit fabric merge %0d sources",SOURCES); $finish;
  end
 end
endmodule
