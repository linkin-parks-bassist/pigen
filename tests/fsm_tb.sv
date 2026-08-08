module fsm_tb;
    logic clk = 0;
    logic reset = 1;
    logic start = 0;
    logic [7:0] source = 8'h5a;
    logic source_valid = 0;
    logic source_ready;
    logic [7:0] destination;
    logic destination_valid;
    logic destination_ready = 1;

    fsm_example dut (
        .clk, .reset, .start, .source, .source_valid, .source_ready,
        .destination, .destination_valid, .destination_ready
    );

    always #5 clk = ~clk;

    initial begin
        repeat (2) @(posedge clk);
        reset = 0;
        @(negedge clk);
        start = 1;
        @(posedge clk);
        @(negedge clk);
        start = 0;
        source_valid = 1;
        @(posedge clk);
        #1;
        source_valid = 0;
        if (!destination_valid || destination != 8'h5a)
            $fatal(1, "FSM state action did not transfer the offered packet");
        $display("PASS: fsm state transition and transport action completed");
        $finish;
    end
endmodule
