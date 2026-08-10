module fifo_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic [7:0] in_packet = '0;
	logic out_ready = 1'b0;
	int offered = 0;
	int received = 0;

	fifo_pipeline dut (.*);

	assign dut.queue__pigen_out_ready = out_ready;

	always #5 clk = ~clk;

	always @(posedge clk)
	begin
		if ($time == 15)
			reset <= 1'b0;

		/* The first pushed item is observable before downstream is ready. */
		if (!reset && $time == 35 && !dut.queue__pigen_valid)
			$fatal(1, "fifo did not expose its first item immediately");

		if (!reset && dut.queue__pigen_valid && out_ready)
		begin
			if (dut.queue !== received[7:0])
				$fatal(1, "fifo ordering failure: expected %0d, got %0d", received, dut.queue);

			received++;
		end

		if (received == 8)
		begin
			$display("PASS: fifo filled under backpressure and delivered %0d values", received);
			$finish;
		end
	end

	always @(negedge clk)
	begin
		/* Preload item 4 while full; it is accepted on the first pop/push cycle. */
		if (!reset && (dut.queue__pigen_in_ready || offered == 4) && offered < 8)
		begin
			in_packet = offered[7:0];
			offered++;
		end

		if (!reset && $time >= 75)
			out_ready = 1'b1;
	end

	initial
	begin
		$dumpfile("examples/fifo_pipeline.vcd");
		$dumpvars(0, fifo_pipeline_tb);
	end

endmodule
