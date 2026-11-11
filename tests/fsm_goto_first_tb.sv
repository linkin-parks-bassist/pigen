module fsm_goto_first_tb;
    logic clk = 0;
    logic reset = 1;
    logic a = 0;
    logic [7:0] marker;
    logic [7:0] after;
    logic [7:0] ticks;
    logic [7:0] other;

    fsm_goto_first dut (
        .clk, .reset, .a, .marker, .after, .ticks, .other
    );

    always #5 clk = ~clk;

    task check(input [7:0] expected_marker, input [7:0] expected_after);
        if (marker !== expected_marker || after !== expected_after)
            $fatal(1, "marker=%0d after=%0d expected marker=%0d after=%0d",
                marker, after, expected_marker, expected_after);
    endtask

    initial begin
        repeat (2) @(posedge clk);
        @(negedge clk);
        reset = 0;
        check(8'd0, 8'd0);
        /* a = 0: the level-zero state action runs. */
        @(posedge clk);
        #1;
        check(8'd0, 8'd9);
        /* a = 1: the first goto wins; the second goto and the trailing
         * level-zero state statement are suppressed, and the earlier action
         * in the same branch stays effective. */
        a = 1;
        @(posedge clk);
        #1;
        check(8'd7, 8'd9);
        /* The destination state's body runs on the next edge, not the goto
         * edge. */
        a = 0;
        @(posedge clk);
        #1;
        check(8'd1, 8'd9);
        if (ticks !== 8'd3)
            $fatal(1, "ticks=%0d expected 3 (level-zero code, reset suppressed)",
                ticks);
        /* The independent process ran on the goto edge too. */
        if (other !== 8'd3)
            $fatal(1, "other=%0d expected 3 (independent process)", other);
        $display("PASS: first goto wins and later FSM actions are suppressed");
        $finish;
    end
endmodule
