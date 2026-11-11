module fabric_static_fanout_tb;
 reg [7:0] data;
 wire [7:0] left, right;
 static_fanout dut(.*);
 initial begin
  for (integer i=0;i<256;i=i+1) begin
   data=i; #1;
   if(left!==data || right!==data) $fatal(1,"static fanout mismatch");
  end
  $display("PASS static fabric fanout"); $finish;
 end
endmodule
