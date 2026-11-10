module fabric_merge_fair_tb;
	logic clk=0, reset=1;
	logic [7:0] s1=8'h11, s2=8'h22, dst;
	logic dst_ready=0;
	logic s1_valid=1, s1_ready, s2_valid=1, s2_ready;
	int got1=0, got2=0, accepted=0;
	top2 dut(.clk(clk), .reset(reset), .s1(s1), .s1_valid(s1_valid), .s1_ready(s1_ready),
		.s2(s2), .s2_valid(s2_valid), .s2_ready(s2_ready), .dst_ready(dst_ready), .dst(dst));
	always #5 clk=~clk;
	always_ff @(posedge clk) if (!reset && dut.mv && dut.mr) begin
		if (dut.m == 8'h11) got1 <= got1 + 1;
		if (dut.m == 8'h22) got2 <= got2 + 1;
		accepted <= accepted + 1;
	end
	initial begin
		repeat(2) @(posedge clk); reset <= 0;
		repeat(5) @(posedge clk);
		@(negedge clk); dst_ready <= 1;
		repeat(8) @(posedge clk);
		if (got1 == 0 || got2 == 0) $fatal(1,"inline merge starved a source: %0d/%0d",got1,got2);
		if (got1 > got2 + 1 || got2 > got1 + 1) $fatal(1,"inline merge grants unfair: %0d/%0d",got1,got2);
		$display("PASS inline fabric fair merge %0d/%0d", got1, got2);
		$finish;
	end
	initial #200 $fatal(1,"timeout");
endmodule
