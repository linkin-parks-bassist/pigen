module generate_if_sv_tb;
  logic [3:0] a = 4'b1010;
  logic [3:0] y;
  genif dut(.a(a), .y(y));
  initial begin
    #1; if (y !== 4'b1010) $fatal(1, "generate-if assignment lost: %b", y);
    $display("PASS conditional generate preserved");
    $finish;
  end
endmodule
