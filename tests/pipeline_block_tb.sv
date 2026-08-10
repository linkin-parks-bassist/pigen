module pipeline_block_tb;
    logic clk = 0, reset = 1, enable = 1;
    logic in_valid = 0, in_ready;
    logic out_valid, out_ready = 0;
    logic [47:0] packet_in = 0;
    logic [15:0] packet_out;
    pipeline_mac dut (.*);
    always #5 clk = ~clk;
    task automatic send(input logic [15:0] m, input logic [15:0] x, input logic [15:0] b);
        @(negedge clk); packet_in = {m, x, b}; in_valid = 1;
        do @(posedge clk); while (!in_ready);
        @(negedge clk); in_valid = 0;
    endtask
    initial begin
        repeat (2) @(posedge clk);
        reset = 0;
        send(16'd3, 16'd7, 16'd2);
        wait (out_valid);
        if (packet_out !== 16'd23) $fatal(1, "pipeline result incorrect");
        repeat (2) begin
            @(posedge clk); #1;
            if (!out_valid || packet_out !== 16'd23)
                $fatal(1, "pipeline did not hold output under backpressure");
        end
        @(negedge clk); out_ready = 1;
        @(posedge clk); @(negedge clk);
        send(16'hffff, 16'd2, 16'd3);
        wait (out_valid);
        if (packet_out !== 16'd1) $fatal(1, "pipeline truncation result incorrect");
        $display("PASS: pipeline language block with backpressure");
        $finish;
    end
endmodule
