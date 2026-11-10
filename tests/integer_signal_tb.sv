module integer_signal_tb;
	logic clk=0, reset=1;
	integer a=-5, y;
	integer_signal dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0;
		repeat(3) @(posedge clk); #1;
		if (y !== -5) $fatal(1,"y=%0d",y);
		$display("PASS integer keyword preserved"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
