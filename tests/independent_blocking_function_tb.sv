module independent_blocking_function_tb;
  logic clk = 0;
  logic [7:0] i = 8'h30, o, temp;
  logic i_valid = 1, i_ready, o_valid, o_ready = 1;
  always #5 clk = ~clk;
  independent_blocking_function dut(.clk(clk), .i(i), .i_valid(i_valid),
    .i_ready(i_ready), .o(o), .o_valid(o_valid), .o_ready(o_ready),
    .temp(temp));
  initial begin
    #22;
    if (temp !== 8'h31) $fatal(1, "blocking function assignment lost: %h", temp);
    if (!o_valid || o !== 8'h30) $fatal(1, "transfer lost: %h %b", o, o_valid);
    $display("PASS independent blocking function split");
    $finish;
  end
endmodule
