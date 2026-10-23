module coslice_xprop_tb;
	logic clk = 1'b0, reset = 1'b1;
	logic [7:0] source = 8'h3c;
	logic source_valid = 1'b0, source_ready;
	logic [7:0] left, previous;
	logic left_valid, left_ready = 1'b1;

	coslice_xprop dut (.*);
	always #5 clk = ~clk;
	initial begin
		$dumpfile("tests/coslice_xprop.vcd"); $dumpvars(0, coslice_xprop_tb);
		#17 reset = 1'b0; source_valid = 1'b1;
		#18;
		if (!left_valid || left != 8'h3d) $fatal(1, "co-slice payload mismatch: left=%h", left);
		if (previous !== 8'h3c) $fatal(1, "previous X-propagation under single-edge stimulus: got %h expected 3c", previous);
		source_valid = 1'b0;
		$display("PASS: co-sliced previous held its pre-edge value under single-edge stimulus"); $finish;
	end
endmodule
