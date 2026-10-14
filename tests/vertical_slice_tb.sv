`timescale 1ns/1ps

module vertical_slice_tb;

    logic clk;
    logic reset;
    logic start;
    logic done;

    vertical_slice dut (
        .clk(clk),
        .reset(reset),
        .start(start),
        .done(done)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        reset = 1;
        start = 0;
        #20;
        reset = 0;
        #10;
        start = 1;
        #10;
        start = 0;
        @(posedge done);
        #100;
        $finish;
    end

endmodule