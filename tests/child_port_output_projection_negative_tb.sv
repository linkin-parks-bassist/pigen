module child_port_endpoint_tb;
 logic clk=0, reset=1, result_ready=0;
 logic [7:0] result; logic result_valid;
 integer queue[0:255], head=0, tail=0, cycle=0;
 endpoint_top dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1;
  reset=cycle<3;
  result_ready=(cycle%5!=0 && cycle%5!=1);
 end
 always @(posedge clk) if(!reset) begin
  if(dut.wire_data[7:0]!==8'ha5) $fatal(1,"payload projection overwrote neighboring bits");
  if(result_valid && result_ready) begin
   if(head>=tail || result!==queue[head][7:0]) $fatal(1,"endpoint ordering at %0d: %0d",head,result);
   head=head+1;
  end
  if(dut.a.o_valid && dut.a.o_ready) begin
   queue[tail]=dut.a.o; tail=tail+1;
  end
  if(cycle==60) begin
   if(head<20) $fatal(1,"insufficient progress %0d",head);
   $display("PASS child port endpoint %0d",head); $finish;
  end
 end
endmodule
