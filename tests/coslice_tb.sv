module coslice_tb;
	logic clk = 1'b0, reset = 1'b1;
	logic [7:0] source = 8'h3c;
	logic source_valid = 1'b0, source_ready;
	logic [7:0] left, right, history, previous;
	logic left_valid, right_valid, left_ready = 1'b1, right_ready = 1'b0;

	coslice dut (.*);
	always #5 clk = ~clk;
	initial begin
		$dumpfile("tests/coslice.vcd"); $dumpvars(0, coslice_tb);
		#17 reset = 1'b0; source_valid = 1'b1;
		#18;
		if (!left_valid || !right_valid || left != 8'h3d || right != 8'h3e || history != 8'h3c) $fatal(1, "co-slice payload mismatch");
		if (right_ready || !right_valid) $fatal(1, "right destination did not retain its atomic copy under stall");
		if (previous != 8'h3c) $fatal(1, "conditional previous wrong under stall: %h", previous);
		right_ready = 1'b1;
		#32;
		if (dut.state_delay != 4'hc) $fatal(1, "last co-sliced destination state_delay wrong: %h", dut.state_delay);
		$display("PASS: co-sliced transfer was atomic"); $finish;
	end
endmodule
