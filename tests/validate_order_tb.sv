module validate_order_tb;
 logic clk=0,reset=1,seed=0; logic [7:0] v1,v2; logic v1_valid,v2_valid,v1_ready=0,v2_ready=0;
 order dut(.*); always #5 clk=~clk;
 initial begin #12 reset=0; seed=1; @(posedge clk); #1; if(v1_valid!==1'b1||v2_valid!==1'b0)$fatal(1,"v1=%b v2=%b",v1_valid,v2_valid); $display("PASS action order"); $finish; end
 initial #100 $fatal(1,"timeout");
endmodule
