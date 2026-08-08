module text_tb;
    localparam int TOTAL_SAMPLES = 10000;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic signed [23:0] data_in = '0;
    logic data_in_valid = 1'b0;
    logic data_in_ready;
    logic signed [23:0] data_out;
    logic data_out_valid;
    logic data_out_ready = 1'b1;

    int sent = 0;
    int received = 0;
    int cycle_count = 0;

    df1_biquad_bandpass dut (.*);

    function automatic logic signed [23:0] stimulus(input int index);
        case (index)
            0: stimulus = 24'sd0;
            1: stimulus = 24'sd1;
            2: stimulus = -24'sd1;
            3: stimulus = 24'sd7;
            4: stimulus = -24'sd12;
            5: stimulus = 24'sd100;
            6: stimulus = -24'sd250;
            7: stimulus = 24'sd1024;
            8: stimulus = -24'sd4096;
            9: stimulus = 24'sd12345;
            10: stimulus = -24'sd23456;
            default: stimulus = (index % 257) - 128;
        endcase
    endfunction

    function automatic logic signed [23:0] expected(input int index);
        expected = $signed(stimulus(index)) * 3
            + (index > 0 ? $signed(stimulus(index - 1)) * 12 : 0);
    endfunction

    always #5 clk = ~clk;

    always @(negedge clk)
    begin
        if (!reset)
        begin
            cycle_count <= cycle_count + 1;
            if (!data_in_valid && sent < TOTAL_SAMPLES)
            begin
                data_in <= stimulus(sent);
                data_in_valid <= 1'b1;
            end
            else if (data_in_valid && data_in_ready)
            begin
                sent <= sent + 1;
                if (sent + 1 < TOTAL_SAMPLES)
                    data_in <= stimulus(sent + 1);
                else
                    data_in_valid <= 1'b0;
            end
        end
    end

    always @(posedge clk)
    begin
        if (!reset && data_out_valid)
        begin
            if (data_out !== expected(received))
                $fatal(1, "text FIR mismatch at sample %0d: got %0d expected %0d",
                    received, data_out, expected(received));

            received <= received + 1;
        end
    end

    initial #17 reset = 1'b0;

    initial
    begin
        $dumpfile("examples/text.vcd");
        $dumpvars(0, text_tb);
    end

    initial
    begin
        repeat (500) @(posedge clk);
        $display("DONE: captured 500 clocks (sent=%0d received=%0d)", sent, received);
        $finish;
    end
endmodule
