module coslice_typedef_tb;
	logic clk = 1'b0, reset = 1'b1;
	logic [15:0] source = 16'hBEEF;
	logic source_valid = 1'b0, source_ready;
	logic [7:0] low, high;
	logic low_valid, high_valid;
	logic low_ready = 1'b1, high_ready = 1'b1;

	coslice_typedef dut (.*);
	always #5 clk = ~clk;
	initial begin
		#17 reset = 1'b0; source_valid = 1'b1;
		#18;
		if (!low_valid || !high_valid)
			$fatal(1, "typedef co-slice destinations not valid: low_valid=%b high_valid=%b", low_valid, high_valid);
		if (low !== 8'hEF) $fatal(1, "low wrong: got %h expected EF", low);
		if (high !== 8'hBE) $fatal(1, "high wrong: got %h expected BE", high);
		$display("PASS: typedef'd co-slice resolved to declared width"); $finish;
	end
endmodule
