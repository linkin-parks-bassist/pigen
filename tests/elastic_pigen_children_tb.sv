module elastic_pigen_children_tb;
 logic clk=0;
 logic [7:0] a=8'ha5; logic a_valid=1; logic a_ready;
 logic [7:0] y; logic y_valid; logic y_ready=1;
 logic [15:0] b=16'hbeef; logic b_valid=1; logic b_ready;
 logic [15:0] z; logic z_valid; logic z_ready=1;
 always #5 clk=~clk;
 top dut(.clk(clk), .a(a), .a_valid(a_valid), .a_ready(a_ready),
  .y(y), .y_valid(y_valid), .y_ready(y_ready),
  .b(b), .b_valid(b_valid), .b_ready(b_ready),
  .z(z), .z_valid(z_valid), .z_ready(z_ready));
 initial begin
  #22;
  if (!y_valid || y !== 8'ha5) $fatal(1,"width8 elastic child failed y=%h valid=%b", y, y_valid);
  if (!z_valid || z !== 16'hbeef) $fatal(1,"width16 elastic child failed z=%h valid=%b", z, z_valid);
  $display("PASS elastic parameterized Pigen child ports");
  $finish;
 end
endmodule
