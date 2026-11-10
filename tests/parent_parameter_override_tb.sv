module parent_parameter_override_tb;
 logic clk=0; logic [15:0] a=16'hcafe; logic av=1; logic ar; logic [15:0] y; logic yv; logic yr=1;
 always #5 clk=~clk;
 top dut(.clk(clk),.a(a),.a_valid(av),.a_ready(ar),.y(y),.y_valid(yv),.y_ready(yr));
 initial begin #22; if(!yv||y!==16'hcafe)$fatal(1,"parent param override failed %h %b",y,yv); $display("PASS parent parameter override"); $finish; end
endmodule
