module nested_guard_chain_dependency_tb;
  logic clk = 0, en = 1, sub = 1;
  logic [7:0] i = 8'd10, o, temp;
  logic i_valid = 1, i_ready, o_valid, o_ready = 1;
  always #5 clk = ~clk;
  mixed_guard_chain dut(.clk(clk), .en(en), .sub(sub), .i(i),
    .i_valid(i_valid), .i_ready(i_ready), .o(o), .o_valid(o_valid),
    .o_ready(o_ready), .temp(temp));
  initial begin
    #6; i = 8'd20; en = 0; sub = 0;
    #10;
    if (temp !== 8'd11) $fatal(1, "temp chain wrong: %0d", temp);
    if (!o_valid || o !== 8'd11) $fatal(1, "guard chain value wrong: %0d %b", o, o_valid);
    $display("PASS nested guard chain mux");
    $finish;
  end
endmodule
