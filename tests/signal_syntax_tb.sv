module signal_syntax_tb;
	logic clk = 1'b0, reset = 1'b1, route_left = 1'b0;
	logic [23:0] source_wide = 24'h12abcd;
	logic source_wide_valid = 1'b0, source_wide_ready;
	logic [7:0] source_tail = 8'he5;
	logic source_tail_valid = 1'b0, source_tail_ready;
	logic [15:0] source_nested = 16'h5aa5;
	logic source_nested_valid = 1'b0, source_nested_ready;
	logic [7:0] source_indexed = 8'hbc;
	logic source_indexed_valid = 1'b0, source_indexed_ready;
	logic [7:0] source_branch = 8'h39;
	logic source_branch_valid = 1'b0, source_branch_ready;
	syntax_packet_t source_struct = 16'hda61;
	logic source_struct_valid = 1'b0, source_struct_ready;
	logic [15:0] source_variable = 16'hd62b;
	logic source_variable_valid = 1'b0, source_variable_ready;
	logic [3:0] variable_base = 4'd4;
	logic [7:0] source_scalar_lvalue = 8'h87;
	logic source_scalar_lvalue_valid = 1'b0, source_scalar_lvalue_ready;
	logic [12:0] held_high;
	logic held_high_valid, held_high_ready = 1'b0;
	logic [3:0] branch_left, branch_right;
	logic branch_left_valid, branch_right_valid;
	logic branch_left_ready = 1'b1, branch_right_ready = 1'b1;
	logic [10:0] state_middle;
	logic [7:0] nested_state;
	logic [3:0] nibble_state;
	logic [7:0] memory_two, memory_one, ordered_state;
	logic [3:0] struct_tag;
	logic [11:0] struct_payload;
	logic [7:0] variable_byte;
	logic [15:0] scalar_lvalue;

	signal_syntax dut (.*);
	always #5 clk = ~clk;

	initial begin
		#12 reset = 1'b0;
		source_wide_valid = 1'b1;
		source_tail_valid = 1'b1;
		source_nested_valid = 1'b1;
		source_indexed_valid = 1'b1;
		source_branch_valid = 1'b1;
		source_struct_valid = 1'b1;
		source_variable_valid = 1'b1;
		source_scalar_lvalue_valid = 1'b1;
		#4;

		if (!held_high_valid || held_high != 13'h1579 ||
			state_middle != 11'h5e5 || memory_two != 8'hc3)
			$fatal(1, "mixed-width buffered/static repartition was wrong");
		if (source_wide_ready || source_tail_ready)
			$fatal(1, "mixed group accepted a second token while its buffer member stalled");
		if (nested_state != 8'h5a || dut.memory[0] != 8'ha5)
			$fatal(1, "nested static concatenation did not use normal SV ordering");
		if (memory_one[7:4] != 4'hb || nibble_state != 4'hc)
			$fatal(1, "memory slice transfer did not use normal SV ordering");
		if (ordered_state != 8'h22)
			$fatal(1, "ordinary source-ordered nonblocking assignments changed meaning");
		if (struct_tag != 4'hd || struct_payload != 12'ha61 || !source_struct_ready)
			$fatal(1, "packed member projections did not transfer as one base token");
		if (variable_byte != 8'h62 || !source_variable_ready)
			$fatal(1, "indexed source part-select did not retain SV behavior");
		if (scalar_lvalue[7:0] != 8'h87 || !source_scalar_lvalue_ready)
			$fatal(1, "scalar ordinary lvalue slice did not consume its signal source");
		if (branch_left_valid || !branch_right_valid || branch_right != 4'h9)
			$fatal(1, "exclusive buffered consumer selected the wrong branch");

		source_wide = 24'hffffff;
		source_tail = 8'hff;
		#10;
		if (state_middle != 11'h5e5 || memory_two != 8'hc3)
			$fatal(1, "static members updated while a grouped buffer destination stalled");

		route_left = 1'b1;
		source_branch = 8'h72;
		#10;
		if (!branch_left_valid || branch_left != 4'h7)
			$fatal(1, "mutually exclusive alternate consumer did not transfer");

		$display("PASS: gnarly signal syntax retained atomic and ordinary-SV behavior");
		$finish;
	end
endmodule
