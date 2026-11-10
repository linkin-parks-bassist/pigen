module pipeline_reset_tb;
	logic clk=0, reset=1, enable=1;
	logic [7:0] source=8'd10, destination;
	logic source_valid=0, source_ready, destination_valid, destination_ready=1;
	pipeline_reset dut(.*); always #5 clk=~clk;
	initial begin
		repeat(3) @(posedge clk); #1;
		if (destination_valid !== 1'b0) $fatal(1,"reset pipeline produced valid output");
		reset=0; source=8'd10; source_valid=1;
		do @(posedge clk); while (!source_ready);
		@(negedge clk); source_valid=0;
		begin int guard=0; while (!destination_valid && guard<20) begin @(posedge clk); guard++; end end
		#1;
		if (destination !== 8'd11) $fatal(1,"destination=%0d",destination);
		$display("PASS pipeline reset binding"); $finish;
	end
	initial #100 $fatal(1,"timeout");
endmodule
