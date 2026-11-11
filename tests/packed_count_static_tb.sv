module packed_count_static_tb;
 wire [7:0] o;
 packed_count_static dut(.*);
 initial begin
  #1;
  if($bits(dut.o)!=8 || o!==8'ha5) $fatal(1,"packed count width");
  $display("PASS packed static count"); $finish;
 end
endmodule
