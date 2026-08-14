module sv_compat_syntax_tb;
	logic clk = 1'b0;
	logic [15:0] source = 16'hd62b;
	logic [2:0] base = 3'd4;
	logic [15:0] state;
	logic [7:0] side;
	sv_compat_syntax dut (.*);
	always #5 clk = ~clk;
	initial begin
		#6;
		$display("state=%04h side=%02h", state, side);
		$finish;
	end
endmodule
