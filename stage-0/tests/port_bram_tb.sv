module port_bram_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic read_enable = 1'b0;
	logic [3:0] read_address = '0;
	logic [3:0] write_address = '0;
	logic data_in_valid = 1'b0;
	logic data_in_ready;
	logic [7:0] data_in = '0;
	int pulses = 0;

	port_bram dut (.*);

	always #5 clk = ~clk;

	initial
	begin
		$dumpfile("tests/port_bram.vcd");
		$dumpvars(0, port_bram_tb);
	end

	always @(negedge clk)
	begin
		if ($time == 10)
			reset <= 1'b0;
		if ($time == 20)
		begin
			data_in_valid <= 1'b1;
			data_in <= 8'hA5;
			write_address <= 4'd3;
			read_address <= 4'd7;
		end
		if ($time == 30)
		begin
			data_in_valid <= 1'b0;
			read_enable <= 1'b1;
			read_address <= 4'd3;
		end
		if ($time == 40)
			read_enable <= 1'b0;
	end

	always @(posedge clk)
	begin
		if (!reset && dut.bram_port__pigen_valid)
		begin
			if (dut.bram_port !== 8'hA5)
				$fatal(1, "BRAM port payload mismatch: got %h", dut.bram_port);
			pulses++;
		end
		if ($time == 35 && dut.mem[3] !== 8'hA5)
			$fatal(1, "input port write did not update BRAM: got %h", dut.mem[3]);
		if ($time == 45)
		begin
			if (pulses != 1)
				$fatal(1, "expected one delayed BRAM port pulse, got %0d", pulses);
			$display("PASS: unconditional BRAM port write produced one delayed valid pulse");
			$finish;
		end
	end
endmodule
