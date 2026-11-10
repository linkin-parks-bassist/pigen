module struct_member_write_tb;
	logic clk=0, reset=1; logic [3:0] tag; logic [11:0] payload;
	struct_member_write dut(.*);
	always #5 clk=~clk;
	initial begin
		#12 reset=0; repeat(3) @(posedge clk); #1;
		if (tag !== 4'ha || payload !== 12'h123) $fatal(1,"tag=%h payload=%h",tag,payload);
		$display("PASS struct member writes"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
