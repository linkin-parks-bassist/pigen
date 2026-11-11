module when_event_tb;
    logic clk = 0;
    logic reset = 1;
    logic [7:0] source = 8'h00;
    logic source_valid = 0;
    logic source_ready;
    logic [7:0] destination;
    logic destination_valid;
    logic destination_ready = 0;
    logic [7:0] fired;
    logic below;

    when_event dut (
        .clk, .reset, .source, .source_valid, .source_ready,
        .destination, .destination_valid, .destination_ready, .fired, .below
    );

    always #5 clk = ~clk;

    initial begin
        repeat (2) @(posedge clk);
        @(negedge clk);
        reset = 0;
        /* No offered value: the event body does not run. */
        @(posedge clk);
        #1;
        if (fired !== 8'd0)
            $fatal(1, "fired=%0d without a transfer", fired);
        if (below !== 1'b1)
            $fatal(1, "below must follow the computed fired <= 1 condition");
        /* The empty destination buffer accepts the offer even though its
         * downstream consumer is not ready yet. */
        source = 8'h5a;
        source_valid = 1;
        @(posedge clk);
        #1;
        if (fired !== 8'd1)
            $fatal(1, "fired=%0d after one accepted transfer", fired);
        /* The held buffer is full, so the same offer does not fire again. */
        @(posedge clk);
        #1;
        if (fired !== 8'd1)
            $fatal(1, "fired=%0d while the destination cannot accept", fired);
        /* Drain the buffer: the held transfer fires and the body runs. */
        destination_ready = 1;
        @(posedge clk);
        #1;
        if (fired !== 8'd2)
            $fatal(1, "fired=%0d after draining and refilling", fired);
        /* Withdraw the offer: the body does not run on the next edge. */
        source_valid = 0;
        @(posedge clk);
        #1;
        if (fired !== 8'd2)
            $fatal(1, "fired=%0d without a further transfer", fired);
        /* The computed relational condition now selects the else branch. */
        if (below !== 1'b0)
            $fatal(1, "below must clear once fired is above one");
        $display("PASS: when fires only with the transfer and if is relational");
        $finish;
    end
endmodule
