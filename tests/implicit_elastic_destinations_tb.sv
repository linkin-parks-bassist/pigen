module implicit_elastic_destinations_tb;
  logic clk = 0;
  logic [7:0] a8 = 8'ha5; logic a8_valid = 1; logic a8_ready;
  logic [7:0] y8; logic y8_valid; logic y8_ready = 1;
  logic [15:0] a16 = 16'hbeef; logic a16_valid = 1; logic a16_ready;
  logic [15:0] y16; logic y16_valid; logic y16_ready = 1;
  always #5 clk = ~clk;
  top dut(.clk(clk),
    .a8(a8), .a8_valid(a8_valid), .a8_ready(a8_ready),
    .y8(y8), .y8_valid(y8_valid), .y8_ready(y8_ready),
    .a16(a16), .a16_valid(a16_valid), .a16_ready(a16_ready),
    .y16(y16), .y16_valid(y16_valid), .y16_ready(y16_ready));
  initial begin
    #32;
    if (!y8_valid || y8 !== 8'ha5) $fatal(1, "width-8 destination failed %h %b", y8, y8_valid);
    if (!y16_valid || y16 !== 16'hbeef) $fatal(1, "width-16 destination failed %h %b", y16, y16_valid);
    $display("PASS implicit elastic destinations");
    $finish;
  end
endmodule
