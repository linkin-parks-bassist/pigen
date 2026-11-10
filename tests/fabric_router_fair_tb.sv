module fabric_router_fair_tb;
	logic clk=0, reset=1, enable=1;
	logic p0_in_valid=0, p0_in_ready;
	logic [5:0] p0_in_packet=0;
	logic p0_out_valid, p0_out_ready=1;
	logic [5:0] p0_out_packet;
	logic p1_in_valid=0, p1_in_ready;
	logic [5:0] p1_in_packet=0;
	logic p1_out_valid, p1_out_ready=0;
	logic [5:0] p1_out_packet;
	logic p2_in_valid=0, p2_in_ready;
	logic [5:0] p2_in_packet=0;
	logic p2_out_valid, p2_out_ready=1;
	logic [5:0] p2_out_packet;
	int seen0=0, seen2=0, accepted=0;
	routed_fabric__fabric_router #(.PAYLOAD_W(4), .PATH_W(2)) dut(.*);
	always #5 clk=~clk;
	always_ff @(posedge clk) if (!reset && p1_out_valid && p1_out_ready) begin
		if (p1_out_packet[5:4] == 2'b00) seen0 <= seen0 + 1;
		if (p1_out_packet[5:4] == 2'b10) seen2 <= seen2 + 1;
		accepted <= accepted + 1;
	end
	initial begin
		repeat (2) @(posedge clk); reset = 0;
		@(negedge clk);
		p0_in_packet = {2'b00, 4'ha}; p0_in_valid = 1;
		p2_in_packet = {2'b01, 4'hb}; p2_in_valid = 1;
		repeat (4) @(posedge clk);
		@(negedge clk); p1_out_ready = 1;
		repeat (8) @(posedge clk);
		if (seen0 == 0 || seen2 == 0) $fatal(1, "router starved a competing source: seen0=%0d seen2=%0d", seen0, seen2);
		if (seen0 > seen2 + 1 || seen2 > seen0 + 1)
			$fatal(1, "router grants were unfair: seen0=%0d seen2=%0d", seen0, seen2);
		$display("PASS: router fairness under continuous contention"); $finish;
	end
	initial #300 $fatal(1,"timeout");
endmodule
