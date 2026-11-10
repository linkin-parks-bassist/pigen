module macro_paste_tb;
	logic clk=0, reset=1, a_valid=0, a_ready, y_valid, y_ready=1;
	logic [7:0] a=8'h40, y;
	macro_paste dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0; a_valid=1;
		do @(posedge clk); while (!a_ready);
		@(negedge clk); a_valid=0;
		begin int guard=0; while (!y_valid && guard<20) begin @(posedge clk); guard++; end end
		#1; if (y !== 8'h08) $fatal(1,"y=%h",y);
		$display("PASS macro token paste"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
