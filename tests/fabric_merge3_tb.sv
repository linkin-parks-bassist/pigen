module fabric_merge3_tb;
	logic clk=0, reset=1, dst_ready=0;
	logic [7:0] s1=8'h11, s2=8'h22, s3=8'h33, dst;
	logic s1_valid=1, s1_ready, s2_valid=1, s2_ready, s3_valid=1, s3_ready;
	int g1=0, g2=0, g3=0;
	top3 dut(.*); always #5 clk=~clk;
	always_ff @(posedge clk) if (!reset && dut.mv && dut.mr) begin
		if (dut.m == 8'h11) g1 <= g1 + 1;
		if (dut.m == 8'h22) g2 <= g2 + 1;
		if (dut.m == 8'h33) g3 <= g3 + 1;
	end
	initial begin
		repeat(2) @(posedge clk); reset <= 0;
		repeat(4) @(posedge clk);
		@(negedge clk); dst_ready <= 1;
		repeat(12) @(posedge clk);
		if (g1 == 0 || g2 == 0 || g3 == 0)
			$fatal(1,"N-merge starved a source: %0d/%0d/%0d",g1,g2,g3);
		if (g1 > g2 + 1 || g2 > g1 + 1 || g1 > g3 + 1 || g3 > g1 + 1 ||
			g2 > g3 + 1 || g3 > g2 + 1)
			$fatal(1,"N-merge grants unfair: %0d/%0d/%0d",g1,g2,g3);
		$display("PASS inline N-source fair merge %0d/%0d/%0d",g1,g2,g3);
		$finish;
	end
	initial #300 $fatal(1,"timeout");
endmodule
