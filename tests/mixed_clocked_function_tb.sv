module mixed_clocked_function_tb;
  logic clk = 0;
  logic [7:0] i = 8'h10;
  logic i_valid = 1;
  logic i_ready;
  logic [7:0] o;
  logic o_valid;
  logic o_ready = 1;
  logic [7:0] temp;
  always #5 clk = ~clk;
  mixed_clocked_function dut(.clk(clk), .i(i), .i_valid(i_valid),
    .i_ready(i_ready), .o(o), .o_valid(o_valid), .o_ready(o_ready),
    .temp(temp));
  initial begin
    #22;
    if (temp !== 8'h11) $fatal(1, "function call lost: %h", temp);
    if (!o_valid || o !== 8'h10) $fatal(1, "transfer lost: %h %b", o, o_valid);
    $display("PASS mixed clocked function and transfer");
    $finish;
  end
endmodule
