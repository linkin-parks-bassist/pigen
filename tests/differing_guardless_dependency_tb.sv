module differing_guardless_dependency_tb;
  logic clk = 0, en1 = 1, en2 = 0;
  logic [7:0] i = 8'd10, o, temp;
  logic i_valid = 1, i_ready, o_valid, o_ready = 1;
  always #5 clk = ~clk;
  mixed_guardless_bad dut(.clk(clk), .en1(en1), .en2(en2), .i(i),
    .i_valid(i_valid), .i_ready(i_ready), .o(o), .o_valid(o_valid),
    .o_ready(o_ready), .temp(temp));
  initial begin
    #6; i = 8'd20; en1 = 0; en2 = 1;
    #10;
    if (temp !== 8'd11) $fatal(1, "temp update wrong: %0d", temp);
    if (!o_valid || o !== 8'd11) $fatal(1, "guarded value wrong: %0d %b", o, o_valid);
    $display("PASS differing guardless dependency");
    $finish;
  end
endmodule
