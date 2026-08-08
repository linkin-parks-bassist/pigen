module fifo_discard_tb;
    logic clk = 0;
    logic reset = 1;
    logic clear = 0;
    logic discard = 0;
    logic in_valid = 0;
    logic in_ready;
    logic [7:0] packet_in = '0;
    logic out_valid;
    logic out_ready = 0;
    logic [7:0] packet_out;

    pigen_fifo #(.PAYLOAD_T(logic [7:0]), .DEPTH(4)) dut (
		.clk, .reset, .clear, .discard,
		.force_valid(1'b0),
		.force_invalid(1'b0),
		.force_after_transfer(1'b0),
		.in_valid, .in_ready, .packet_in,
        .out_valid, .out_ready, .packet_out
    );

    always #5 clk = ~clk;

    initial begin
        repeat (2) @(posedge clk);
        reset = 0;
        packet_in = 8'h11; in_valid = 1;
        @(posedge clk);
        packet_in = 8'h22;
        @(posedge clk);
        @(negedge clk);
        in_valid = 0;
        discard = 1;
        @(posedge clk);
        #1;
        discard = 0;
        if (!out_valid || packet_out != 8'h22)
            $fatal(1, "invalidate must discard exactly one FIFO item");
        @(negedge clk);
        clear = 1;
        @(posedge clk);
        #1;
        clear = 0;
        if (out_valid)
            $fatal(1, "flush must clear remaining FIFO items");
        $display("PASS: FIFO invalidate drops one item and flush clears all");
        $finish;
    end
endmodule
