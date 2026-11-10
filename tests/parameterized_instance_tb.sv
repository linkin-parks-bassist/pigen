module parameterized_instance_tb;
  logic clk = 0;
  logic a_valid = 1;
  bit [15:0] a = 16'hbeef;
  bit [15:0] y;
  always #5 clk = ~clk;
  top dut(.clk(clk), .a(a), .a_valid(a_valid), .a_ready(), .y(y));
  initial begin
    #12;
    if (y !== 16'hbeef) $fatal(1, "parameterized child did not forward %h", y);
    $display("PASS parameterized child instance");
    $finish;
  end
endmodule
