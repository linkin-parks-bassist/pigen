module pipeline_tb;
    logic clk = 1'b0, reset = 1'b1, enable = 1'b1;
    logic [7:0] source, destination;
    logic source_valid = 1'b0, source_ready, destination_valid, destination_ready = 1'b1;

    pipeline dut (.*);
    always #5 clk = ~clk;

    int sent, received, cycles;
    int disabled_checks, x_checks, z_checks;
    logic [7:0] expected [0:15];

    always @(negedge clk) if (!reset) begin
        case (cycles % 13)
            2,3,4: enable = 1'b0;
            7: enable = 1'bx;
            8: enable = 1'bz;
            default: enable = 1'b1;
        endcase
    end

    always @(posedge clk) begin
        if (reset) begin
            destination_ready <= 1'b1;
			cycles <= 0;
        end else begin
            /* Deliberately stop the downstream consumer while the pipeline is
             * full.  A correctly-elaborated pipeline must retain every token. */
            cycles <= cycles + 1;
            destination_ready <= !((cycles % 11) >= 4 && (cycles % 11) < 8);
            if (source_valid && enable !== 1'b1) begin
                if (source_ready !== 1'b0)
                    $fatal(1, "disabled/unknown pipeline guard accepted input");
                disabled_checks++;
                if (enable === 1'bx) x_checks++;
                if (enable === 1'bz) z_checks++;
            end
            if (source_valid && source_ready) sent <= sent + 1;
            if (destination_valid && destination_ready) begin
                if (destination !== expected[received])
					$fatal(1, "pipeline reordered/lost payload %0d", received);
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
        if (!disabled_checks || !x_checks || !z_checks)
            $fatal(1, "pipeline guard stimulus did not exercise 0/X/Z");
        $display("PASS: pipeline is elastic through downstream stalls");
        $finish;
    end

    initial begin
        repeat (200) @(posedge clk);
        $fatal(1, "pipeline elastic test timed out: sent=%0d received=%0d", sent, received);
    end
endmodule
