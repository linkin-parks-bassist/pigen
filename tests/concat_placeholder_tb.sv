module concat_placeholder_tb;
	logic clk=0, reset=1;
	logic [7:0] a, b, c, x, y;
	logic a_valid=0, b_valid=0, c_valid=0, a_ready, b_ready, c_ready, x_valid, y_valid, x_ready=1, y_ready=1;
	ph dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0; a=8'h20; b=8'h5a; c=8'h77; a_valid=1; b_valid=1; c_valid=1;
		repeat(3) @(posedge clk); #1;
		if (x !== 8'h21 || y !== 8'h77) $fatal(1,"x=%h y=%h",x,y);
		$display("PASS concat discard placeholder"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
