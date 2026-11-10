module enum_preserve_tb;
  logic clk = 0;
  logic en = 1;
  logic [1:0] y;
  always #5 clk = ~clk;
  m dut(.clk(clk), .en(en), .y(y));
  initial begin
    #12;
    if (y !== 2'd1) $fatal(1, "enum assignment lost: %0d", y);
    $display("PASS enum-typed variable preserved");
    $finish;
  end
endmodule
