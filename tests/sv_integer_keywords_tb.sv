module sv_integer_keywords_tb;
	logic clk=0, reset=1;
	shortint a=-7, sa;
	longint b=64'sd1234567890123, lb;
	time t=64'd42, tt;
	sv_integer_keywords dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0;
		repeat(3) @(posedge clk); #1;
		if (sa !== -7 || lb !== 64'sd1234567890123 || tt !== 64'd42)
			$fatal(1,"sa=%0d lb=%0d tt=%0d",sa,lb,tt);
		$display("PASS shortint/longint/time preserved"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
