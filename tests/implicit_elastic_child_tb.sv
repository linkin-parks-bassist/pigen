module implicit_elastic_child_tb;
 logic clk=0; logic [7:0] a=8'hc3; logic av=1; logic ar; logic [7:0] y;
 always #5 clk=~clk;
 top dut(.clk(clk),.a(a),.a_valid(av),.a_ready(ar),.y(y));
 initial begin #22; if(y!==8'hc3)$fatal(1,"implicit input endpoint failed %h",y); $display("PASS implicit parent input endpoint"); $finish; end
endmodule
