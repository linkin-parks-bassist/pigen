module port_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;

	logic in_packet_valid = 1'b0;
	logic in_packet_ready;
	logic [7:0] in_packet = '0;

	int sent = 0;
	int received = 0;

	port_pipeline dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (reset)
		begin
			in_packet_valid = 1'b0;
		end
		else if (sent < 6)
		begin
			in_packet_valid = (sent % 2) == 0;
			in_packet = sent[7:0];
			sent++;
		end
		else
			in_packet_valid = 1'b0;
	end

	always @(posedge clk)
	begin
		if ($time == 15)
			reset <= 1'b0;

		if (!reset && dut.pulse__pigen_valid)
		begin
			if (dut.pulse !== 8'(received * 2))
				$fatal(1, "port pulse mismatch: expected %0d, got %0d", received * 2, dut.pulse);

			received++;
		end

		if (received == 3)
		begin
			$display("PASS: port produced %0d one-cycle valid pulses", received);
			$finish;
		end
	end

	initial
	begin
		$dumpfile("examples/port_pipeline.vcd");
		$dumpvars(0, port_pipeline_tb);
	end

endmodule
