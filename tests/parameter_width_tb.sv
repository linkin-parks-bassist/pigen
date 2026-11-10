module parameter_width_tb;
	logic clk=0, reset=1, a_valid=0, a_ready, y_valid, y_ready=1;
	logic [7:0] a=8'hc3, y;
	paramod dut(.*); always #5 clk=~clk;
	initial begin
		#12 reset=0; a_valid=1;
		do @(posedge clk); while (!a_ready);
		@(negedge clk); a_valid=0;
		begin int guard=0; while (y_valid !== 1'b1 && guard<20) begin @(posedge clk); guard++; end end
		#1; if (y !== 8'hc3) $fatal(1,"y=%h",y);
		$display("PASS parameter-dependent width"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
