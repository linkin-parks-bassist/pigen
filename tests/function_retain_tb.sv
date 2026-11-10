module function_retain_tb;
	logic clk=0; logic [7:0] a=8'h41; logic [7:0] y;
	fn dut(.*);
	always #5 clk=~clk;
	initial begin
		#1;
		if (y !== 8'h42) $fatal(1,"y=%h",y);
		$display("PASS retained function call"); $finish;
	end
endmodule
