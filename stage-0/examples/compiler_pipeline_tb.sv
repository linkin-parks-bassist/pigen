module compiler_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic [7:0] a = '0;
	logic [7:0] b = '0;
	logic [7:0] delayed_sum;
	logic delayed_sum_valid;
	logic delayed_sum_ready = 1'b1;

	int offered = 0;
	int received = 0;

	compiler_pipeline dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (!reset && offered < 8)
		begin
			a = offered[7:0];
			b = 8'(offered * 2);
			offered++;
		end
	end

	always @(posedge clk)
	begin
		if (!reset && delayed_sum_valid && delayed_sum_ready)
		begin
			if (delayed_sum !== 8'(received * 3))
				$fatal(1, "expected %0d, got %0d", received * 3, delayed_sum);

			received++;
		end

		if ($time == 15)
			reset <= 1'b0;

		if (received == 8)
		begin
			$display("PASS: compiler-lowered expression pipeline delivered %0d results", received);
			$finish;
		end
	end

	initial
	begin
		$dumpfile("examples/compiler_pipeline.vcd");
		$dumpvars(0, compiler_pipeline_tb);
	end

endmodule
