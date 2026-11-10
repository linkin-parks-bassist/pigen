module parameterized_pigen_children_tb;
  logic clk = 0;
  logic [7:0] a8 = 8'ha5;
  logic [7:0] y8;
  logic [15:0] a16 = 16'hbeef;
  logic [15:0] y16;
  always #5 clk = ~clk;
  top dut(.clk(clk), .a8(a8), .y8(y8), .a16(a16), .y16(y16));
  initial begin
    #12;
    if (y8 !== 8'ha5) $fatal(1, "width-8 child failed: %h", y8);
    if (y16 !== 16'hbeef) $fatal(1, "width-16 child failed: %h", y16);
    $display("PASS parameterized Pigen children");
    $finish;
  end
endmodule
