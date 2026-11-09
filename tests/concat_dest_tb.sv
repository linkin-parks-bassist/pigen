module cc_tb; logic clk=0,reset=1; logic[7:0] a,b,x,y; logic a_valid=0,b_valid=0,a_ready,b_ready,x_valid,y_valid; logic x_ready=1,y_ready=1;
 cc dut(.*); always #5 clk=~clk;
 initial begin #12 reset=0; a=5; b=9; a_valid=1; b_valid=1; end
 always @(posedge clk) if(!reset && x_valid && y_valid && x_ready && y_ready) begin
  $display("x=%0d y=%0d",x,y); if(x!==5||y!==9)$fatal(1,"bad"); $display("PASS concat"); $finish; end
 initial #100 $fatal(1,"timeout");
endmodule
