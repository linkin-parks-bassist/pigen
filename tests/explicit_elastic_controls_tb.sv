module explicit_elastic_controls_tb;
  logic clk = 0;
  logic [7:0] a = 8'h77, y;
  logic av = 1, ar, yv, yr = 1;
  always #5 clk = ~clk;
  top dut(.clk(clk), .a(a), .av(av), .ar(ar), .y(y), .yv(yv), .yr(yr));
  initial begin
    #22;
    if (!yv || y !== 8'h77) $fatal(1, "explicit controls failed: %h %b", y, yv);
    $display("PASS explicit elastic controls");
    $finish;
  end
endmodule
