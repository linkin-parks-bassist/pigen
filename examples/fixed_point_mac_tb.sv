module fixed_point_mac_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic [15:0] sample = '0;
	logic sample_valid = 1'b0;
	logic sample_ready;
	logic [15:0] coefficient = '0;
	logic coefficient_valid = 1'b0;
	logic coefficient_ready;
	logic [15:0] bias = '0;
	logic bias_valid = 1'b0;
	logic bias_ready;
	logic [15:0] result;
	logic result_valid;
	logic result_ready = 1'b1;
	int sent = 0;
	int received = 0;
	int phase = 0;
	int cycles = 0;

	fixed_point_mac dut (.*);

	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (reset)
		begin
			sample_valid <= 1'b0;
			coefficient_valid <= 1'b0;
			bias_valid <= 1'b0;
		end
		else
		begin
			cycles <= cycles + 1;
			result_ready <= (cycles % 4) != 1;
			case (phase)
				0: if (sent < 4)
				begin
					sample <= 16'((sent + 1) * 256);
					coefficient <= 16'd384; // 1.5 in 8.8 fixed point
					bias <= 16'd16;
					sample_valid <= 1'b1;
					coefficient_valid <= 1'b1;
					bias_valid <= 1'b1;
					phase <= 1;
				end
				1: if (sample_ready && coefficient_ready)
				begin
					sample_valid <= 1'b0;
					coefficient_valid <= 1'b0;
					phase <= 2;
				end
				2: if (result_valid)
				begin
					bias_valid <= 1'b0;
					phase <= 3;
				end
				3: if (received == sent + 1)
				begin
					sent <= sent + 1;
					phase <= 0;
				end
				default: ;
			endcase
		end
	end

	always @(posedge clk)
	begin
		if ($time == 15)
			reset <= 1'b0;
		if (!reset && result_valid && result_ready)
		begin
			if (result !== 16'((received + 1) * 384 + 16))
				$fatal(1, "fixed-point MAC mismatch: got %0d", result);
			received++;
		end
		if (received == 4)
		begin
			$display("PASS: fixed-point MAC delivered %0d ordered results", received);
			$finish;
		end
	end

	initial
	begin
		$dumpfile("examples/fixed_point_mac.vcd");
		$dumpvars(0, fixed_point_mac_tb);
	end
endmodule
