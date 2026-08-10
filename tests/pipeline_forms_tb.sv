module pipeline_forms_tb;
    logic clk = 0, reset = 1, enable = 1, in_valid = 0, out_ready = 1;
    logic [17:0] packet_in;
    logic in_ready, out_valid;
    logic [8:0] packet_out;
    signed_pipeline dut (.*);
    always #5 clk = ~clk;
    initial begin
        repeat (2) @(posedge clk); reset = 0;
        @(negedge clk); packet_in = {-8'sd3, -8'sd4, 2'b00}; in_valid = 1;
        do @(posedge clk); while (!in_ready);
        @(negedge clk); in_valid = 0;
        wait (out_valid);
        if ($signed(packet_out) !== -9'sd7)
            $fatal(1, "expected signed pipeline result -7, got %0d", $signed(packet_out));
        $display("PASS: typed, signed, inherited pipeline tuples");
        $finish;
    end
endmodule
