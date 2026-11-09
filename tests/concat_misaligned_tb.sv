module concat_misaligned_tb;
  logic clk=0, reset=1; logic [15:0] s; logic [7:0] x,y;
  logic s_valid=0, s_ready, x_valid, y_valid, x_ready=1, y_ready=1;
  cm dut(.*); always #5 clk=~clk;
  initial begin #12 reset=0; s=16'hABCD; s_valid=1; end
  always @(posedge clk) if (!reset && x_valid && y_valid && x_ready && y_ready) begin
    if (x !== 8'hAB || y !== 8'hDC) $fatal(1,"x=%h y=%h",x,y);
    $display("PASS misaligned concat x=%h y=%h", x, y); $finish;
  end
  initial #100 $fatal(1,"timeout");
endmodule
