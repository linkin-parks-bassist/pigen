module slicing_concat_tb;
	logic clk = 1'b0, reset = 1'b1;
	logic [3:0] join_high = 4'ha;
	logic [11:0] join_low = 12'hbcd;
	logic join_high_valid = 1'b0, join_low_valid = 1'b0;
	logic join_high_ready, join_low_ready;
	logic [15:0] joined;
	logic joined_valid, joined_ready = 1'b1;
	logic [15:0] whole = 16'habcd;
	logic whole_valid = 1'b0, whole_ready;
	logic [5:0] split_high;
	logic [9:0] split_low;
	logic split_high_valid, split_low_valid;
	logic split_high_ready = 1'b1, split_low_ready = 1'b0;
	logic [15:0] single_sliced = 16'h19e7;
	logic single_sliced_valid = 1'b0, single_sliced_ready;
	logic [7:0] selected_byte;
	logic selected_byte_valid, selected_byte_ready = 1'b0;
	logic [15:0] sliced = 16'h5ac3;
	logic sliced_valid = 1'b0, sliced_ready;
	logic [7:0] slice_high, slice_low;
	logic slice_high_valid, slice_low_valid;
	logic slice_high_ready = 1'b1, slice_low_ready = 1'b0;
	logic [7:0] constant_high, constant_low;
	logic constant_high_valid, constant_low_valid;
	logic constant_high_ready = 1'b0, constant_low_ready = 1'b0;

	slicing_concat dut (.*);
	always #5 clk = ~clk;

	initial begin
		#12 reset = 1'b0;
		join_high_valid = 1'b1;
		join_low_valid = 1'b1;
		whole_valid = 1'b1;
		single_sliced_valid = 1'b1;
		sliced_valid = 1'b1;
		#4;
		if (!joined_valid || joined != 16'habcd)
			$fatal(1, "RHS concatenation did not join atomically");
		if (whole_ready || !split_high_valid || !split_low_valid ||
			split_high != 6'h2a || split_low != 10'h3cd)
			$fatal(1, "whole packet was not retained atomically after one split destination stalled");
		if (sliced_ready || !slice_high_valid || !slice_low_valid ||
			slice_high != 8'h5a || slice_low != 8'hc3)
			$fatal(1, "sliced packet was not retained atomically after one co-slice destination stalled");
		if (single_sliced_ready || !selected_byte_valid || selected_byte != 8'he7)
			$fatal(1, "a selected payload did not consume and stall the complete base packet");
		if (!constant_high_valid || !constant_low_valid ||
			constant_high != 8'hca || constant_low != 8'hfe)
			$fatal(1, "constant source did not behave as an always-valid bit stream");

		split_low_ready = 1'b1;
		slice_low_ready = 1'b1;
		#10;
		if (!split_high_valid || !split_low_valid || split_high != 6'h2a || split_low != 10'h3cd)
			$fatal(1, "flattened whole-packet split had the wrong payload");
		if (!slice_high_valid || !slice_low_valid || slice_high != 8'h5a || slice_low != 8'hc3)
			$fatal(1, "co-sliced packet split had the wrong payload");
		$display("PASS: concatenation and co-slicing use one atomic bit-stream transfer");
		$finish;
	end
endmodule
