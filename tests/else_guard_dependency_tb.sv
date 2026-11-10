module else_guard_dependency_tb;
 logic clk=0,en=0; logic [7:0] i=8'd10; logic iv=1; logic ir; logic [7:0] o; logic ov; logic orr=1; logic [7:0] temp;
 always #5 clk=~clk;
 mixed_else dut(.clk(clk),.en(en),.i(i),.i_valid(iv),.i_ready(ir),.o(o),.o_valid(ov),.o_ready(orr),.temp(temp));
 initial begin
  #6; en=1; i=8'd20;
  #10;
  if(temp!==8'd11)$fatal(1,"temp %h",temp);
  if(!ov||o!==8'd11)$fatal(1,"else value %h %b",o,ov);
  $display("PASS else guard mux"); $finish;
 end
endmodule
