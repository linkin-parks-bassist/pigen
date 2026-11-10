module peek_tb;
	logic clk=0, reset=1;
	logic [7:0] source=8'h5a, dest;
	logic source_valid=0, source_ready, dest_valid, dest_ready=1;
	peekm dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0;
		repeat(3) @(posedge clk); #1;
		if (dest_valid !== 1'b1 || dest !== 8'h5a) $fatal(1,"dest=%h valid=%b",dest,dest_valid);
		if (source_ready !== 1'b0) $fatal(1,"peek consumed source or left ready undefined: %b",source_ready);
		$display("PASS peek does not consume tokens"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
