module output_pipeline_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;

	logic [7:0] in_packet = '0;
	logic in_packet_valid = 1'b0;
	logic in_packet_ready;

	logic [7:0] out_packet;
	logic out_packet_valid;
	logic out_packet_ready = 1'b1;

	int next_input = 0;
	int next_output = 0;
	int cycles = 0;

	output_pipeline dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (reset)
		begin
			in_packet_valid = 1'b0;
			out_packet_ready = 1'b1;
		end
		else
		begin
			out_packet_ready = (cycles % 4) != 1;

			if (!in_packet_valid && next_input < 10)
			begin
				in_packet_valid = 1'b1;
				in_packet = next_input[7:0];
			end
			else if (in_packet_valid && in_packet_ready)
			begin
				next_input++;

				if (next_input == 10)
					in_packet_valid = 1'b0;
				else
					in_packet = 8'(next_input);
			end
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

		if (cycles == 50)
			$fatal(1, "timeout: sent=%0d received=%0d in_valid=%b in_ready=%b out_valid=%b out_ready=%b", next_input, next_output, in_packet_valid, in_packet_ready, out_packet_valid, out_packet_ready);

		if (next_output == 10)
		begin
			$display("PASS: output buf port delivered %0d ordered values", next_output);
			$finish;
		end
	end

	initial
	begin
		#17 reset = 1'b0;
	end

	initial
	begin
		$dumpfile("examples/output_pipeline.vcd");
		$dumpvars(0, output_pipeline_tb);
	end

endmodule
