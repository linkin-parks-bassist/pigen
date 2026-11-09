module signed_cast_tb;
  logic clk=0, reset=1; logic [7:0] a; logic [7:0] y; logic a_valid=0, a_ready;
  cast dut(.clk(clk), .reset(reset), .a(a), .a_valid(a_valid), .a_ready(a_ready), .y(y));
  always #5 clk=~clk;
  initial begin #12 reset=0; a=8'hA5; a_valid=1; @(posedge clk); #1;
    if (y !== 8'hA5) $fatal(1,"y=%h",y);
    $display("PASS signed cast %h", y); $finish;
  end
  initial #100 $fatal(1,"timeout");
endmodule
