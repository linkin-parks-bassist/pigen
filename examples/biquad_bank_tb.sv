module biquad_bank_tb;
	/* Keep the Verilator VCD intentionally human-scale. Only the explicitly
	 * re-enabled probe declarations below are traced. */
	/*verilator tracing_off*/
	localparam int FILTERS = 8;
	localparam int SAMPLES_PER_FILTER = 384;
	localparam int TOTAL_REQUESTS = FILTERS * SAMPLES_PER_FILTER;
	/* Additional settling cycles after all previous-round writebacks complete. */
	localparam int WRITEBACK_GAP = 16;
	localparam logic signed [17:0] COEF_ONE = 18'sd8192;

	biquad_req req_in;
	logic req_in_valid = 1'b0, req_in_ready;
	biquad_out bqd_out;
	logic bqd_out_valid, bqd_out_ready = 1'b1;
	biquad_coef_write_req coef_write_port;
	logic coef_write_port_valid = 1'b0, coef_write_port_ready;
	/* Simple top-level waveform probes. These are deliberately scalar named
	 * traces, rather than relying on a viewer's unpacked-array support. */
	/*verilator tracing_on*/
	logic clk = 1'b0, reset = 1'b1, enable = 1'b1;
	logic signed [15:0] sample_in = 16'sd1024;
	logic signed [15:0] biquad_0_out, biquad_1_out, biquad_2_out, biquad_3_out;
	logic signed [15:0] biquad_4_out, biquad_5_out, biquad_6_out, biquad_7_out;
	/*verilator tracing_off*/

	/* External trace registers: inspect these eight arrays independently in the
	 * VCD. They are intentionally testbench state, not DUT implementation. */
	logic signed [15:0] filter0 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter1 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter2 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter3 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter4 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter5 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter6 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] filter7 [0:SAMPLES_PER_FILTER-1];
	logic signed [15:0] expected [0:TOTAL_REQUESTS-1];
	longint signed model_x1 [0:FILTERS-1], model_x2 [0:FILTERS-1];
	longint signed model_y1 [0:FILTERS-1], model_y2 [0:FILTERS-1];
	int sent, received, cycle_count, last_burst_cycle = -1;

	biquad_pipeline dut (.*);
	always #5 clk = ~clk;

	/* RBJ Audio EQ Cookbook coefficients, Q2.12. The DUT's recurrence is
	 * b0*x + b1*x1 + b2*x2 + a1*y1 + a2*y2, so denominator coefficients
	 * are stored with the sign already inverted. */
	function automatic logic signed [17:0] coefficient(input int filter, input int tap);
		case (filter)
			0: case (tap) 0: coefficient = 189;  1: coefficient = 378;   2: coefficient = 189;  3: coefficient = 5354;  default: coefficient = -2014; endcase // LP, f=.08, Q=.707
			1: case (tap) 0: coefficient = 2866; 1: coefficient = -5732; 2: coefficient = 2866; 3: coefficient = 5354;  default: coefficient = -2014; endcase // HP, f=.08, Q=.707
			2: case (tap) 0: coefficient = 1276; 1: coefficient = 0;     2: coefficient = -1276;3: coefficient = 2402;  default: coefficient = -1544; endcase // BP, f=.18, Q=1
			3: case (tap) 0: coefficient = 2820; 1: coefficient = -2402; 2: coefficient = 2820; 3: coefficient = 2402;  default: coefficient = -1544; endcase // notch, f=.18, Q=1
			4: case (tap) 0: coefficient = 5162; 1: coefficient = 0;     2: coefficient = 889;  3: coefficient = 0;     default: coefficient = -1954; endcase // peak, f=.25, +6dB, Q=1
			5: case (tap) 0: coefficient = 4486; 1: coefficient = -6203; 2: coefficient = 2389; 3: coefficient = 6371;  default: coefficient = -2612; endcase // low shelf, f=.06, +6dB
			6: case (tap) 0: coefficient = 3090; 1: coefficient = 1729;  2: coefficient = 703;  3: coefficient = -702;  default: coefficient = -724; endcase // high shelf, f=.30, -6dB
			default: case (tap) 0: coefficient = 1115; 1: coefficient = -3063; 2: coefficient = 4096; 3: coefficient = 3063;  default: coefficient = -1115; endcase // all-pass, f=.15, Q=.707
		endcase
	endfunction

	/* Shared musical material: impulse/ring-out, a rising chirp, a two-tone
	 * passage, then an alternating high-frequency tail. */
	function automatic logic signed [15:0] stimulus(input int sample_index);
		real phase;
		real sample;
		begin
			if (sample_index == 0) sample = 0.80;
			else if (sample_index < 32) sample = 0.0;
			else if (sample_index < 176) begin
				phase = 2.0 * 3.141592653589793 * (sample_index - 32) * (sample_index - 32) / (2.0 * 144.0 * 3.0);
				sample = 0.62 * $sin(phase);
			end else if (sample_index < 304)
				sample = 0.35 * $sin(2.0 * 3.141592653589793 * sample_index / 31.0) +
				0.25 * $sin(2.0 * 3.141592653589793 * sample_index / 9.0);
			else sample = sample_index[0] ? 0.45 : -0.45;
			stimulus = $rtoi(sample * 16384.0);
		end
	endfunction

	function automatic logic signed [15:0] saturate(input longint signed value);
		begin
			if (value > 32767) saturate = 16'sd32767;
			else if (value < -32768) saturate = -16'sd32768;
			else saturate = value[15:0];
		end
	endfunction

	task automatic write_filter(input logic [2:0] handle,
		input logic signed [17:0] b0, input logic signed [17:0] b1,
		input logic signed [17:0] b2, input logic signed [17:0] a1,
		input logic signed [17:0] a2);
		begin
			@(negedge clk);
			coef_write_port = {handle, b0, b1, b2, a1, a2};
			coef_write_port_valid = 1'b1;
			do @(posedge clk); while (coef_write_port_ready !== 1'b1);
			@(negedge clk);
			coef_write_port_valid = 1'b0;
		end
	endtask

	task automatic send_state_safe_requests;
		begin
			for (int round = 0; round < SAMPLES_PER_FILTER; round++) begin
				@(negedge clk);
				sample_in = stimulus(round);
				for (int handle = 0; handle < FILTERS; handle++) begin
					if (handle != 0) @(negedge clk);
					req_in = {handle[2:0], sample_in};
					req_in_valid = 1'b1;
					do @(posedge clk); while (req_in_ready !== 1'b1);
				end
				@(negedge clk);
				req_in_valid = 1'b0;
				/* Output acceptance is the state writeback event. Do not revisit a
				 * handle until every result from this round has committed. */
				while (received < (round + 1) * FILTERS) @(posedge clk);
				if (round + 1 < SAMPLES_PER_FILTER) repeat (WRITEBACK_GAP) @(posedge clk);
			end
		end
	endtask

	always @(posedge clk) begin
		longint signed accumulator;
		longint signed shifted;
		int handle;
		if (reset) begin
			cycle_count <= 0;
			last_burst_cycle <= -1;
		end else begin
			cycle_count <= cycle_count + 1;
			/* Ports are deliberately always-ready single-cycle endpoints. This
			 * demo uses one, so its external consumer stays ready. A separate
			 * inline-pipeline test uses an output buf and exercises stalls. */
			bqd_out_ready <= 1'b1;
			if (req_in_valid && req_in_ready) begin
				handle = req_in.handle;
				accumulator = $signed(req_in.sample) * coefficient(handle, 0) +
					model_x1[handle] * coefficient(handle, 1) + model_x2[handle] * coefficient(handle, 2) +
					model_y1[handle] * coefficient(handle, 3) + model_y2[handle] * coefficient(handle, 4);
				shifted = accumulator >>> 12;
				expected[sent] = saturate(shifted);
				model_x2[handle] = model_x1[handle]; model_x1[handle] = $signed(req_in.sample);
				model_y2[handle] = model_y1[handle]; model_y1[handle] = expected[sent];
				if (sent < FILTERS && last_burst_cycle >= 0 && cycle_count != last_burst_cycle + 1)
					$fatal(1, "bank stopped during full-rate burst: cycles %0d and %0d", last_burst_cycle, cycle_count);
				if (sent < FILTERS) last_burst_cycle <= cycle_count;
				sent <= sent + 1;
			end
			if (bqd_out_valid && bqd_out_ready) begin
				if (bqd_out.sample !== expected[received])
					$fatal(1, "filter result mismatch %0d: got %0d expected %0d", received, bqd_out.sample, expected[received]);
				case (received % FILTERS)
					0: begin filter0[received / FILTERS] <= bqd_out.sample; biquad_0_out <= bqd_out.sample; end
					1: begin filter1[received / FILTERS] <= bqd_out.sample; biquad_1_out <= bqd_out.sample; end
					2: begin filter2[received / FILTERS] <= bqd_out.sample; biquad_2_out <= bqd_out.sample; end
					3: begin filter3[received / FILTERS] <= bqd_out.sample; biquad_3_out <= bqd_out.sample; end
					4: begin filter4[received / FILTERS] <= bqd_out.sample; biquad_4_out <= bqd_out.sample; end
					5: begin filter5[received / FILTERS] <= bqd_out.sample; biquad_5_out <= bqd_out.sample; end
					6: begin filter6[received / FILTERS] <= bqd_out.sample; biquad_6_out <= bqd_out.sample; end
					7: begin filter7[received / FILTERS] <= bqd_out.sample; biquad_7_out <= bqd_out.sample; end
				endcase
				received <= received + 1;
			end
		end
	end

	initial begin
		$dumpfile("examples/biquad_bank.vcd");
		$dumpvars(0, clk, reset, sample_in,
			biquad_0_out, biquad_1_out, biquad_2_out, biquad_3_out,
			biquad_4_out, biquad_5_out, biquad_6_out, biquad_7_out);
		#17 reset = 1'b0;
		repeat (2) @(posedge clk);
		for (int i = 0; i < FILTERS; i++)
			write_filter(i[2:0], coefficient(i, 0), coefficient(i, 1), coefficient(i, 2), coefficient(i, 3), coefficient(i, 4));
		repeat (3) @(posedge clk);
		send_state_safe_requests();
		repeat (WRITEBACK_GAP + 8) @(posedge clk);
		if (sent != TOTAL_REQUESTS || received != TOTAL_REQUESTS)
			$fatal(1, "bank lost work: sent=%0d received=%0d", sent, received);
		$display("PASS: eight-filter bank stateful outputs match reference model");
		$finish;
	end

	initial begin
		repeat (32768) @(posedge clk);
		$fatal(1, "biquad bank timed out (sent=%0d received=%0d)", sent, received);
	end
endmodule
