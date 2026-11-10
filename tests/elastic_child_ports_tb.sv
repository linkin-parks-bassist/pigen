module elastic_child_ports_tb;
 logic clk=0; logic [7:0] a=8'h5a; logic a_valid=1; logic a_ready;
 logic [7:0] y; logic y_valid; logic y_ready=1;
 always #5 clk=~clk;
 top dut(.clk(clk),.a(a),.a_valid(a_valid),.a_ready(a_ready),.y(y),.y_valid(y_valid),.y_ready(y_ready));
 initial begin #22; if(!y_valid||y!==8'h5a)$fatal(1,"failed %h %b",y,y_valid); $display("PASS elastic child ports"); $finish; end
endmodule
