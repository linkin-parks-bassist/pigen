module macro_stringify_tb;
  logic clk=0; always #5 clk=~clk;
  strmacro dut(.*);
  initial begin #25 $finish; end
endmodule
