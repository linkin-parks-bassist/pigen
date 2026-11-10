module else_if_guard_dependency_tb;
  logic clk = 0, a = 0, b = 1;
  logic [7:0] i = 8'd10, o, temp;
  logic i_valid = 1, i_ready, o_valid, o_ready = 1;
  always #5 clk = ~clk;
  mixed_elseif dut(.clk(clk), .a(a), .b(b), .i(i), .i_valid(i_valid),
    .i_ready(i_ready), .o(o), .o_valid(o_valid), .o_ready(o_ready),
    .temp(temp));
  initial begin
    #6; a = 1; b = 0; i = 8'd20;
    #10;
    if (temp !== 8'd11) $fatal(1, "else-if temp wrong: %0d", temp);
    if (!o_valid || o !== 8'd11) $fatal(1, "else-if value wrong: %0d %b", o, o_valid);
    $display("PASS else-if guard mux");
    $finish;
  end
endmodule
