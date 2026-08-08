module fabric_router_tb;
    logic clk = 0, reset = 1, enable = 1;
    logic p0_in_valid = 0, p0_in_ready;
    logic [5:0] p0_in_packet = 0;
    logic p0_out_valid, p0_out_ready = 1;
    logic [5:0] p0_out_packet;
    logic p1_in_valid = 0, p1_in_ready;
    logic [5:0] p1_in_packet = 0;
    logic p1_out_valid, p1_out_ready = 1;
    logic [5:0] p1_out_packet;
    logic p2_in_valid = 0, p2_in_ready;
    logic [5:0] p2_in_packet = 0;
    logic p2_out_valid, p2_out_ready = 1;
    logic [5:0] p2_out_packet;
    native_fabric__fabric_router #(.PAYLOAD_W(4), .PATH_W(2)) dut (.*);
    always #5 clk = ~clk;
    initial begin
        repeat (2) @(posedge clk); reset = 0;
        @(negedge clk);
        p0_in_packet = {2'b00, 4'ha}; p0_in_valid = 1;
        p2_in_packet = {2'b01, 4'hb}; p2_in_valid = 1;
        @(posedge clk); @(negedge clk); p0_in_valid = 0; p2_in_valid = 0;
        if (!p1_out_valid || p1_out_packet !== {2'b00, 4'ha})
            $fatal(1, "first router arbitration result incorrect");
        @(posedge clk); #1;
        if (!p1_out_valid || p1_out_packet !== {2'b10, 4'hb})
            $fatal(1, "round-robin router result incorrect");
        $display("PASS: native fabric router arbitration");
        $finish;
    end
endmodule
