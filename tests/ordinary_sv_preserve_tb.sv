module ordinary_sv_preserve_tb;
  logic clk = 0;
  logic [3:0] a = 4'b1010;
  logic [3:0] y, z;
  ordinary_sv_preserve dut(.clk(clk), .a(a), .y(y), .z(z));
  initial begin
    #1;
    if (y !== 4'b1010) $fatal(1, "generate assignment lost: %b", y);
    if (z !== 4'b0101) $fatal(1, "always_comb assignment lost: %b", z);
    $display("PASS ordinary SV preserved");
    $finish;
  end
endmodule
