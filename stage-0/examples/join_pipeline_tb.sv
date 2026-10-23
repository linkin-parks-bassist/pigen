module join_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;

	logic [7:0] left = '0;
	logic left_valid = 1'b0;
	logic left_ready;

	logic [7:0] right = '0;
	logic right_valid = 1'b0;
	logic right_ready;

	int offered = 0;
	int received = 0;

	join_pipeline dut (.*);

	assign dut.sum__pigen_out_ready = 1'b1;

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (reset)
		begin
			left_valid = 1'b0;
			right_valid = 1'b0;
		end
		else
		begin
			left_valid = 1'b1;
			right_valid = 1'b1;

			if (left_ready && right_ready && offered < 8)
			begin
				left = offered[7:0];
				right = 8'(offered * 2);
				offered++;
			end
		end
	end

	always @(posedge clk)
	begin
		if ($time == 15)
			reset <= 1'b0;

		if (!reset && dut.sum__pigen_valid)
		begin
			if (dut.sum !== 8'(received * 3))
				$fatal(1, "join mismatch: expected %0d, got %0d", received * 3, dut.sum);

			received++;
		end

		if (received == 8)
		begin
			$display("PASS: atomic join delivered %0d results", received);
			$finish;
		end
	end

	initial
	begin
		$dumpfile("examples/join_pipeline.vcd");
		$dumpvars(0, join_pipeline_tb);
	end

endmodule
