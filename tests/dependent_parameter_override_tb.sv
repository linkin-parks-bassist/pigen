module dependent_parameter_override_tb;
  logic clk = 0;
  logic [15:0] a = 16'h1234, y;
  logic av = 1, ar, yv, yr = 1;
  logic [31:0] tag;
  always #5 clk = ~clk;
  top dut(.clk(clk), .a(a), .a_valid(av), .a_ready(ar), .y(y),
    .y_valid(yv), .y_ready(yr), .tag(tag));
  initial begin
    #22;
    if (!yv || y !== 16'h1234) $fatal(1, "dependent width transfer failed: %h %b", y, yv);
    if (tag !== 32'd0) $fatal(1, "dependent width tag failed: %h", tag);
    $display("PASS dependent parameter override");
    $finish;
  end
endmodule
