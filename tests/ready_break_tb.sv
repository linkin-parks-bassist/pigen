module ready_break_tb;
    logic clk = 0;
    logic reset = 1;
    logic in_valid = 0;
    logic out_ready = 0;
    logic [7:0] packet_in = 0;
    logic fifo_in_ready;
    logic fifo_out_valid;
    logic [7:0] fifo_packet_out;
    logic skid_in_ready;
    logic skid_out_valid;
    logic [7:0] skid_packet_out;

    always #5 clk = ~clk;

    pigen_fifo #(.PAYLOAD_T(logic [7:0]), .DEPTH(2)) fifo_dut (
        .clk, .reset, .clear(1'b0), .discard(1'b0),
        .force_valid(1'b0), .force_invalid(1'b0),
        .force_after_transfer(1'b0), .in_valid,
        .in_ready(fifo_in_ready), .packet_in,
        .out_valid(fifo_out_valid), .out_ready,
        .packet_out(fifo_packet_out)
    );

    pigen_skid #(.PAYLOAD_T(logic [7:0])) skid_dut (
        .clk, .reset, .clear(1'b0), .discard(1'b0),
        .force_valid(1'b0), .force_invalid(1'b0),
        .force_after_transfer(1'b0), .in_valid,
        .in_ready(skid_in_ready), .packet_in,
        .out_valid(skid_out_valid), .out_ready,
        .packet_out(skid_packet_out)
    );

    initial begin
        repeat (2) @(posedge clk);
        @(negedge clk);
        reset = 0;
        in_valid = 1;
        packet_in = 8'h10;

        @(posedge clk);
        @(negedge clk);
        packet_in = 8'h11;
        @(posedge clk);
        #1;
        if (fifo_in_ready || skid_in_ready)
            $fatal(1, "full FIFO/skid unexpectedly ready");

        // No clock edge: downstream readiness must not leak upstream.
        out_ready = 1;
        #1;
        if (fifo_in_ready || skid_in_ready)
            $fatal(1, "downstream ready propagated combinationally through FIFO/skid");

        @(posedge clk);
        #1;
        if (!fifo_in_ready || !skid_in_ready)
            $fatal(1, "registered readiness did not return after pop");

        $display("PASS: FIFO and skid break combinational ready propagation");
        $finish;
    end
endmodule
