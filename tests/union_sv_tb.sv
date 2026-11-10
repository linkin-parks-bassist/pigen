module union_sv_tb;
  logic clk = 0;
  logic [7:0] d = 8'h5a, q;
  always #5 clk = ~clk;
  union_m dut(.clk(clk), .d(d), .q(q));
  initial begin
    #12;
    if (q !== 8'h5a) $fatal(1, "packed union assignment lost: %h", q);
    $display("PASS packed union module preserved");
    $finish;
  end
endmodule
