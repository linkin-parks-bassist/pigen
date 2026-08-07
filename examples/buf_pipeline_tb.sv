module buf_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;

	logic in_packet_valid = 1'b0;
	logic in_packet_ready;
	logic [7:0] in_packet = '0;

	logic out_packet_valid;
	logic out_packet_ready = 1'b1;
	logic [7:0] out_packet;

	int next_input = 0;
	int next_output = 0;
	int cycles = 0;

	buf_pipeline dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (reset)
		begin
			in_packet_valid = 1'b0;
			out_packet_ready = 1'b1;
		end
		else if (next_input < 12 && (!in_packet_valid || in_packet_ready))
		begin
			out_packet_ready = (cycles % 5) != 2 && (cycles % 5) != 3;
			in_packet_valid = 1'b1;
			in_packet = next_input[7:0];
			next_input++;
		end
		else
		begin
			out_packet_ready = (cycles % 5) != 2 && (cycles % 5) != 3;

			if (next_input == 12)
				in_packet_valid = 1'b0;
		end
	end

	always @(posedge clk)
	begin
		if (!reset && out_packet_valid && out_packet_ready)
		begin
			if (out_packet !== next_output[7:0])
				$fatal(1, "expected %0d, got %0d", next_output, out_packet);

			next_output++;
		end

		cycles++;

		if (cycles == 3)
			reset <= 1'b0;

		if (next_output == 12)
		begin
			$display("PASS: delivered %0d ordered packets", next_output);
			$finish;
		end
	end

	initial
	begin
		$dumpfile("examples/buf_pipeline.vcd");
		$dumpvars(0, buf_pipeline_tb);
	end

endmodule
