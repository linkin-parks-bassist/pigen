module pipeline_scope_tb;
	logic clk = 1'b0, reset = 1'b1, enable = 1'b1;
	logic [7:0] static_source = 8'd40, wire_source, first_source, later_source;
	logic wire_source_valid = 1'b0, wire_source_ready;
	logic first_source_valid = 1'b0, first_source_ready;
	logic later_source_valid = 1'b0, later_source_ready;
	logic [7:0] wire_result, later_result;
	logic wire_result_valid, wire_result_ready = 1'b1;
	logic later_result_valid, later_result_ready = 1'b1;
	logic [7:0] static_result;
	logic static_result_valid, static_result_ready = 1'b1;
	int wire_seen, later_seen, static_seen;

	pipeline_scope dut (.*);
	always #5 clk = ~clk;

	always @(posedge clk) begin
		if (!reset && wire_result_valid && wire_result_ready) begin
			if (wire_result !== 8'd42)
				$fatal(1, "stage-local wire result was %0d", wire_result);
			wire_seen <= wire_seen + 1;
		end
		if (!reset && later_result_valid && later_result_ready) begin
			if (later_result !== 8'd42)
				$fatal(1, "later-stage joined result was %0d", later_result);
			later_seen <= later_seen + 1;
		end
		if (!reset && static_result_valid && static_result_ready) begin
			if (static_result !== 8'd42)
				$fatal(1, "module wire/localparam result was %0d", static_result);
			static_seen <= static_seen + 1;
		end
	end

	initial begin
		#12 reset = 1'b0;
		@(negedge clk);
		wire_source = 8'd41;
		wire_source_valid = 1'b1;
		first_source = 8'd17;
		first_source_valid = 1'b1;
		do @(posedge clk); while (!wire_source_ready || !first_source_ready);
		@(negedge clk);
		wire_source_valid = 1'b0;
		first_source_valid = 1'b0;

		/* The second stage must retain the first packet without consuming a
		 * missing module-scope input. */
		repeat (3) @(posedge clk);
		if (later_seen != 0 || later_source_ready !== 1'b1)
			$fatal(1, "later-stage input did not participate in the stage handshake");
		@(negedge clk);
		later_source = 8'd25;
		later_source_valid = 1'b1;
		do @(posedge clk); while (!later_source_ready);
		@(negedge clk) later_source_valid = 1'b0;

		while (wire_seen != 1 || later_seen != 1 || static_seen == 0) @(posedge clk);
		$display("PASS: stage-local scope and later-stage module inputs preserve handshakes");
		$finish;
	end

	initial begin
		repeat (100) @(posedge clk);
		$fatal(1, "pipeline scope test timed out");
	end
endmodule
