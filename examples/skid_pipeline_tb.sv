module skid_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic [7:0] in_packet = '0;
	logic out_ready = 1'b0;
	int offered = 0;
	int received = 0;

	skid_pipeline dut (.*);

	assign dut.queue__pigen_out_ready = out_ready;

	always #5 clk = ~clk;

	always @(posedge clk)
	begin
		if ($time == 15)
			reset <= 1'b0;

		if (!reset && dut.queue__pigen_valid && out_ready)
		begin
			if (dut.queue !== received[7:0])
				$fatal(1, "skid delivery failure: expected %0d, got %0d", received, dut.queue);

			received++;
		end

		if (received == 6)
		begin
			$display("PASS: skid buffer stalled and drained %0d values", received);
			$finish;
		end
	end

	always @(negedge clk)
	begin
		if (!reset)
		begin
			if ((dut.queue__pigen_in_ready || offered == 2) && offered < 6)
			begin
				in_packet = offered[7:0];
				offered++;
			end

			if ($time >= 55)
				out_ready = 1'b1;
		end
	end

	initial
	begin
		$dumpfile("examples/skid_pipeline.vcd");
		$dumpvars(0, skid_pipeline_tb);
	end

endmodule
