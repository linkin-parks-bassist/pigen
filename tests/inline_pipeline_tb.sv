module inline_pipeline_tb;
    logic clk = 1'b0, reset = 1'b1, enable = 1'b1;
    logic [7:0] source = 8'h2a, destination;
    logic source_valid = 1'b0, source_ready, destination_valid, destination_ready = 1'b1;

    inline_pipeline dut (.*);
    always #5 clk = ~clk;

    initial begin
        #12 reset = 1'b0;
        @(negedge clk); source_valid = 1'b1;
        @(negedge clk); source_valid = 1'b0;
        repeat (4) @(negedge clk);
        if (!destination_valid || destination != 8'h2b) $fatal(1, "inline pipeline payload mismatch");
        $display("PASS: inline pipeline carries packed declarations and anonymous stages");
        $finish;
    end
endmodule
