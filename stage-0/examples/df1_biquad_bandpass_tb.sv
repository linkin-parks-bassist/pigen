module df1_biquad_bandpass_tb #(
	parameter bit STRICT = 1'b1,
	parameter bit STOP_ON_COMPLETE = 1'b1
);
	localparam int TOTAL_SAMPLES = 512;
	localparam int Q23 = 1 << 23;
	localparam longint signed B0 = 191477;
	localparam longint signed B1 = 0;
	localparam longint signed B2 = -191477;
	localparam longint signed A1 = -1849065;
	localparam longint signed A2 = 952838;

	logic clk = 1'b0;
	logic reset = 1'b1;
	logic signed [23:0] sample = '0;
	logic sample_valid = 1'b0;
	logic sample_ready;
	logic signed [23:0] filtered;
	logic filtered_valid;
	logic filtered_ready = 1'b1;

	logic signed [23:0] expected [0:TOTAL_SAMPLES-1];
	longint signed model_x1, model_x2, model_y1, model_y2, model_acc, model_shifted;
	int sent, received, cycle_count;
	bit tolerated_startup_zero;
	real low_sum, pass_sum, high_sum, chirp_sum;

	df1_biquad_bandpass dut (.*);

	function automatic logic signed [23:0] q1_23(input real value);
		integer scaled;
		begin
			scaled = $rtoi(value * Q23);
			if (scaled > 8388607) scaled = 8388607;
			if (scaled < -8388608) scaled = -8388608;
			q1_23 = scaled;
		end
	endfunction

	function automatic logic signed [23:0] stimulus(input int n);
		real phase;
		begin
			// An impulse reveals the ring; then low/pass/high tones and a sweep.
			if (n == 0)
				stimulus = q1_23(0.80);
			else if (n < 96)
				stimulus = q1_23(0.70 * $sin(2.0 * 3.141592653589793 * n / 64.0));
			else if (n < 192)
				stimulus = q1_23(0.70 * $sin(2.0 * 3.141592653589793 * n / 16.0));
			else if (n < 288)
				stimulus = q1_23(0.70 * $sin(2.0 * 3.141592653589793 * n / 4.0));
			else begin
				phase = 2.0 * 3.141592653589793 * (n - 288) * (n - 288) / (2.0 * 224.0 * 8.0);
				stimulus = q1_23(0.70 * $sin(phase));
			end
		end
	endfunction

	always #5 clk = ~clk;

	// Hold source-valid until accepted.  The occasional sink stall makes the
	// VCD show the Pigen ready chain, while normal operation dispatches every clock.
	always @(negedge clk)
	begin
		if (!reset)
		begin
			cycle_count <= cycle_count + 1;
			filtered_ready <= (cycle_count % 37) != 11;
			if (!sample_valid && sent < TOTAL_SAMPLES)
		begin
				sample <= stimulus(sent);
				sample_valid <= 1'b1;
			end
			else if (sample_valid && sample_ready)
			begin
				model_acc = $signed(sample) * B0 + model_x1 * B1 + model_x2 * B2 - model_y1 * A1 - model_y2 * A2;
				model_shifted = model_acc >>> 20;
				if (model_shifted > 8388607) expected[sent] = 24'sd8388607;
				else if (model_shifted < -8388608) expected[sent] = -24'sd8388608;
				else expected[sent] = model_shifted[23:0];
				model_x2 = model_x1;
				model_x1 = $signed(sample);
				model_y2 = model_y1;
				model_y1 = $signed(expected[sent]);
				sent <= sent + 1;
				if (sent + 1 < TOTAL_SAMPLES)
					sample <= stimulus(sent + 1);
				else
					sample_valid <= 1'b0;
			end
		end
	end

	always @(posedge clk)
	begin
		if (!reset && filtered_valid && filtered_ready)
		begin
			if (!tolerated_startup_zero && received == 0 && filtered === '0)
			begin
				tolerated_startup_zero = 1'b1;
				$display("INFO: ignored leading zero startup token");
			end
			else begin
				if (filtered !== expected[received]) begin
					if (STRICT)
						$fatal(1, "DF1 mismatch at sample %0d: got %0d expected %0d", received, filtered, expected[received]);
					else
						$display("MISMATCH: sample %0d: got %0d expected %0d", received, filtered, expected[received]);
				end
				if (received >= 32 && received < 96) low_sum += $itor(filtered < 0 ? -filtered : filtered);
				if (received >= 128 && received < 192) pass_sum += $itor(filtered < 0 ? -filtered : filtered);
				if (received >= 224 && received < 288) high_sum += $itor(filtered < 0 ? -filtered : filtered);
				if (received >= 288) chirp_sum += $itor(filtered < 0 ? -filtered : filtered);
				received <= received + 1;
				if (received + 1 == TOTAL_SAMPLES)
				begin
					if (!(pass_sum > low_sum * 2.0 && pass_sum > high_sum * 2.0))
						$fatal(1, "band-pass contrast too small: low=%f pass=%f high=%f", low_sum, pass_sum, high_sum);
					$display("PASS: %0d samples; |pass|=%0f, |low|=%0f, |high|=%0f, chirp=%0f", TOTAL_SAMPLES, pass_sum, low_sum, high_sum, chirp_sum);
					if (STOP_ON_COMPLETE)
						$finish;
				end
			end
		end
	end

	initial #17 reset = 1'b0;

	initial
	begin
		$dumpfile("examples/df1_biquad_bandpass.vcd");
		$dumpvars(0, df1_biquad_bandpass_tb);
	end

	initial
	begin
		repeat (1024) @(posedge clk);
		if (STRICT)
			$fatal(1, "DF1 biquad timed out after 1024 clock cycles (sent=%0d received=%0d)", sent, received);
		else begin
			$display("INFO: stopped after 1024 clock cycles (sent=%0d received=%0d)", sent, received);
			$finish;
		end
	end
endmodule
