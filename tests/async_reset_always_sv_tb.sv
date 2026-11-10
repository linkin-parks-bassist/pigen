module async_reset_always_sv_tb;
  logic clk = 0, reset = 1;
  logic [7:0] d = 8'h5a, q;
  always #5 clk = ~clk;
  async_reset2 dut(.clk(clk), .reset(reset), .d(d), .q(q));
  initial begin
    #1; reset = 0;
    #1; if (q !== 8'd0) $fatal(1, "async reset failed: %h", q);
    reset = 1;
    #10; if (q !== 8'h5a) $fatal(1, "clocked assignment failed: %h", q);
    $display("PASS async-reset always block preserved");
    $finish;
  end
endmodule
