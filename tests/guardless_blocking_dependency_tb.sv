module guardless_blocking_dependency_tb;
 logic clk=0,en=1; logic [7:0] i=8'h30; logic iv=1; logic ir; logic [7:0] o; logic ov; logic orr=1; logic [7:0] temp;
 always #5 clk=~clk;
 mixed_guardless_good dut(.clk(clk),.en(en),.i(i),.i_valid(iv),.i_ready(ir),.o(o),.o_valid(ov),.o_ready(orr),.temp(temp));
 initial begin #22; if(temp!==8'h31)$fatal(1,"temp %h",temp); if(!ov||o!==8'h31)$fatal(1,"o %h %b",o,ov); $display("PASS guardless good"); $finish; end
endmodule
