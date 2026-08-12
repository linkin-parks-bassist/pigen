module inline_pipeline_tb;
    logic clk = 1'b0, reset = 1'b1, enable = 1'b1;
    logic [7:0] source, destination;
    logic source_valid = 1'b0, source_ready, destination_valid, destination_ready = 1'b1;

    inline_pipeline dut (.*);
    always #5 clk = ~clk;

    int sent, received, cycles;
    logic [7:0] expected [0:15];

    always @(posedge clk) begin
        if (reset) begin
            destination_ready <= 1'b1;
        end else begin
            /* Deliberately stop the downstream consumer while the pipeline is
             * full.  A correctly-elaborated pipeline must retain every token. */
            cycles <= cycles + 1;
            destination_ready <= !((cycles % 11) >= 4 && (cycles % 11) < 8);
            if (source_valid && source_ready) sent <= sent + 1;
            if (destination_valid && destination_ready) begin
                if (destination !== expected[received])
                    $fatal(1, "inline pipeline reordered/lost payload %0d", received);
                received <= received + 1;
            end
        end
    end

    initial begin
        #12 reset = 1'b0;
        for (int i = 0; i < 16; i++) begin
            expected[i] = i + 8'd1;
            @(negedge clk);
            source = i;
            source_valid = 1'b1;
            do @(posedge clk); while (!source_ready);
        end
        @(negedge clk) source_valid = 1'b0;
        while (received < 16) @(posedge clk);
        $display("PASS: inline pipeline is elastic through downstream stalls");
        $finish;
    end

    initial begin
        repeat (200) @(posedge clk);
        $fatal(1, "inline pipeline elastic test timed out: sent=%0d received=%0d", sent, received);
    end
endmodule
