module ternary_literal_tb;
  logic clk=0, reset=1; logic [7:0] a,b,y;
  logic a_valid=0,b_valid=0,a_ready,b_ready,y_valid,y_ready=1;
  e dut(.*); always #5 clk=~clk;
  initial begin
    #12 reset=0; a=0; b=8'h5a; a_valid=1; b_valid=1;
    @(posedge clk); @(posedge clk); #1;
    if (y !== 8'h00) $fatal(1,"false arm %h",y);
    a=1; @(posedge clk); @(posedge clk); #1;
    if (y !== 8'h5a) $fatal(1,"true arm %h",y);
    $display("PASS ternary based literal"); $finish;
  end
  initial #150 $fatal(1,"timeout");
endmodule
