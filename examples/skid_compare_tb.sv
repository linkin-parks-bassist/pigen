module skid_compare_tb;
	logic clk = 1'b0;
	logic reset = 1'b1;
	logic plain_in_valid = 1'b1;
	logic plain_in_ready;
	logic [7:0] plain_in = '0;
	logic plain_out_valid;
	logic plain_out_ready = 1'b0;
	logic [7:0] plain_out;
	logic skid_in_valid = 1'b1;
	logic skid_in_ready;
	logic [7:0] skid_in = '0;
	logic skid_out_valid;
	logic skid_out_ready = 1'b0;
	logic [7:0] skid_out;
	int plain_sent = 0;
	int skid_sent = 0;
	int plain_received = 0;
	int skid_received = 0;
	int cycles = 0;

	skid_compare dut (.*);
	always #5 clk = ~clk;

	always @(negedge clk)
	begin
		if (!reset)
		begin
			plain_out_ready = cycles >= 3;
			skid_out_ready = cycles >= 3;
			cycles++;
			if ((plain_in_ready || cycles == 3) && plain_sent < 8)
			begin plain_in = plain_sent[7:0]; plain_sent++; end
			if ((skid_in_ready || cycles == 3) && skid_sent < 8)
			begin skid_in = skid_sent[7:0]; skid_sent++; end
		end
	end

	always @(posedge clk)
	begin
		if ($time == 15) reset <= 1'b0;
		if (!reset && plain_out_valid && plain_out_ready)
		begin if (plain_out !== plain_received[7:0]) $fatal(1, "plain ordering"); plain_received++; end
		if (!reset && skid_out_valid && skid_out_ready)
		begin if (skid_out !== skid_received[7:0]) $fatal(1, "skid ordering"); skid_received++; end
		if (!reset && cycles == 3 && skid_sent <= plain_sent)
			$fatal(1, "skid lane did not absorb the extra stalled item");
		if (plain_received == 8 && skid_received == 8)
		begin $display("PASS: skid lane delayed backpressure"); $finish; end
	end

	initial begin $dumpfile("examples/skid_compare.vcd"); $dumpvars(0, skid_compare_tb); end
endmodule
