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
		$display("PASS: co-sliced transfer was atomic"); $finish;
	end
endmodule
