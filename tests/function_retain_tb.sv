module function_retain_tb;
	logic clk=0; logic [7:0] a=8'h41; logic [7:0] y, z;
	fn dut(.*);
	always #5 clk=~clk;
	initial begin
		@(posedge clk); #1;
		if (y !== 8'h42 || z !== 8'h42) $fatal(1,"y=%h z=%h",y,z);
		$display("PASS retained function call"); $finish;
	end
endmodule
