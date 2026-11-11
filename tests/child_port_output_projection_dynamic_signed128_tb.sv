module child_port_endpoint_tb;
 parameter WIDTH=1, MODE=0;
 logic clk=0, reset=1, result_ready=0;
 logic signed [127:0] selector=-128'sd8;
 logic [WIDTH-1:0] result; logic result_valid;
 bit [WIDTH-1:0] queue[0:255];
 integer head=0, tail=0, cycle=0;
 endpoint_top dut(.*);
 always #5 clk=~clk;
 always @(negedge clk) begin
  cycle=cycle+1;
  selector=cycle%2 ? -128'sd8 : (128'sd1<<100);
  reset=cycle<3;
  result_ready=(cycle%5!=0 && cycle%5!=1);
 end
 always @(posedge clk) if(!reset) begin
  if(result_valid && result_ready) begin
   if(head>=tail || result!==queue[head][WIDTH-1:0]) $fatal(1,"endpoint ordering at %0d: %0d",head,result);
   head=head+1;
  end
  if(dut.a.o_valid && dut.a.o_ready) begin
   case(MODE)
    3: queue[tail]=dut.wire_data[dut.selector-5'sd8];
    1: queue[tail]=dut.wire_data[dut.selector +: WIDTH];
    2: queue[tail]=dut.wire_data[dut.selector -: WIDTH];
    default: queue[tail]=dut.wire_data[dut.selector];
   endcase tail=tail+1;
  end
  if(cycle==60) begin
   if(head<20) $fatal(1,"insufficient progress %0d",head);
   $display("PASS child port endpoint %0d",head); $finish;
  end
 end
endmodule
