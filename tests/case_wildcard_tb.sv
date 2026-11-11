module case_wildcard_tb;
 logic clk=0; logic [1:0] sel=0;
 logic [7:0] oz,ox,tz,tx; logic vz,vx,rz,rx;
 mixed_casez z(.clk(clk),.sel(sel),.i(8'd0),.i_valid(1'b1),.i_ready(rz),.o(oz),.o_valid(vz),.o_ready(1'b1),.temp(tz));
 mixed_casex x(.clk(clk),.sel(sel),.i(8'd0),.i_valid(1'b1),.i_ready(rx),.o(ox),.o_valid(vx),.o_ready(1'b1),.temp(tx));
 always #5 clk=~clk;
 task check(input logic [1:0] value);
 logic [7:0] ez,ex;
 begin
  @(negedge clk); sel=value;
  casez(value)
   2'b1?: ez=11;
   2'b?1: ez=21;
   default: ez=31;
  endcase
  casex(value)
   2'b1?: ex=11;
   2'b?1: ex=21;
   default: ex=31;
  endcase
  @(posedge clk); #1;
  if(!vz || oz!==ez || tz!==ez || !vx || ox!==ex || tx!==ex)
   $fatal(1,"wildcard %b: z=%0d/%0d x=%0d/%0d",value,oz,ez,ox,ex);
 end
 endtask
 initial begin
  check(2'b00);check(2'b01);check(2'b10);check(2'b11);
  check(2'bz0);check(2'b0z);check(2'bx0);check(2'b0x);
  check(2'bxx);check(2'bzz);check(2'bxz);check(2'bzx);
  $display("PASS wildcard case guards"); $finish;
 end
endmodule
