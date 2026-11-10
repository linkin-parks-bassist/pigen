module nba_dependency_split_tb;
 logic clk=0; logic [7:0] i=8'h40; logic iv=1; logic ir; logic [7:0] o; logic ov; logic orr=1; logic [7:0] temp;
 always #5 clk=~clk;
 mixed_nba_dep dut(.clk(clk),.i(i),.i_valid(iv),.i_ready(ir),.o(o),.o_valid(ov),.o_ready(orr),.temp(temp));
 initial begin
  #12; if(temp!==8'h41)$fatal(1,"nba function lost %h",temp);
  #10; if(!ov||o!==8'h41)$fatal(1,"nba dependency lost %h %b",o,ov);
  $display("PASS NBA dependency split"); $finish;
 end
endmodule
