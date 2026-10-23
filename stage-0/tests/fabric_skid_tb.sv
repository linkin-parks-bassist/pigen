module fabric_skid_tb;
    logic clk = 0, reset = 1, enable = 1;
    logic in_valid = 0, in_ready;
    logic [7:0] packet_in = 0;
    logic out_valid, out_ready = 1;
    logic [7:0] packet_out;
    integer received = 0;
    routed_fabric__fabric_skid #(.PACKET_W(8)) dut (.*);
    always #5 clk = ~clk;
    always @(posedge clk) begin
        if (!reset && out_valid && out_ready) begin
            if (packet_out !== received[7:0])
                $fatal(1, "expected endpoint packet %0d, got %0d", received, packet_out);
            received <= received + 1;
        end
    end
    initial begin
        repeat (2) @(posedge clk); reset = 0;
        for (integer value = 0; value < 8; value = value + 1) begin
            @(negedge clk); packet_in = value; in_valid = 1;
            @(posedge clk);
            if (!in_ready) $fatal(1, "endpoint queue stalled before packet %0d", value);
        end
        @(negedge clk); in_valid = 0;
        wait (received == 8);
        $display("PASS: fabric endpoint queue full throughput");
        $finish;
    end
endmodule
