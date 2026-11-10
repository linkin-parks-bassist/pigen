module positional_elastic_child_tb;
 logic clk=0; logic [7:0] a=8'h66; logic av=1; logic ar; logic [7:0] y; logic yv; logic yr=1;
 always #5 clk=~clk;
 top dut(.clk(clk),.a(a),.a_valid(av),.a_ready(ar),.y(y),.y_valid(yv),.y_ready(yr));
 initial begin #22; if(!yv||y!==8'h66)$fatal(1,"positional elastic failed %h %b",y,yv); $display("PASS positional elastic child"); $finish; end
endmodule
