module guarded_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic enable = 1'b0;
	logic [7:0] in_packet = '0;
	logic in_packet_valid = 1'b0;
	logic in_packet_ready;
	logic [7:0] out_packet;
	logic out_packet_valid;
	logic out_packet_ready = 1'b1;
	int sent = 0;
	int received = 0;
	int cycles = 0;

	guarded_pipeline dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (!reset)
		begin
			enable = (cycles % 3) != 1;
			out_packet_ready = 1'b1;
			cycles++;

			if (!in_packet_valid && sent < 8)
			begin
				in_packet_valid = 1'b1;
				in_packet = sent[7:0];
			end
			else if (in_packet_valid && in_packet_ready)
			begin
				sent++;
				in_packet = sent[7:0];
				if (sent == 8)
					in_packet_valid = 1'b0;
			end
		end
	end

	always @(posedge clk)
	begin
		if (!reset && out_packet_valid && out_packet_ready)
		begin
			if (out_packet !== received[7:0])
				$fatal(1, "guarded pipeline expected %0d, got %0d", received, out_packet);

			received++;
		end

		if (received == 8)
		begin
			$display("PASS: guarded pipeline delivered %0d ordered values", received);
			$finish;
		end
	end

	initial #17 reset = 1'b0;

	initial
	begin
		$dumpfile("examples/guarded_pipeline.vcd");
		$dumpvars(0, guarded_pipeline_tb);
	end
endmodule
