module triggered_counter_tb;
    logic clk = 0;
    logic reset = 1;
    logic count = 0;
    logic [7:0] value;
    logic active;
    logic previous_count = 0;
    int expected = 0;

    triggered_counter dut (.*);
    always #5 clk = ~clk;

    task automatic drive(input logic next_count);
        @(negedge clk);
        count = next_count;
        @(posedge clk);
        #1;
        if (previous_count)
            expected++;
        if (value !== expected[7:0])
            $fatal(1, "previous_count=%b count=%b expected=%0d value=%0d",
                previous_count, next_count, expected, value);
        if (active !== next_count)
            $fatal(1, "validity did not follow count: count=%b active=%b", next_count, active);
        previous_count = next_count;
    endtask

    initial begin
        $dumpfile("tests/triggered_counter.vcd");
        $dumpvars(0, triggered_counter_tb);

        repeat (2) @(posedge clk);
        #1;
        if (value !== 0)
            $fatal(1, "counter did not reset to zero: value=%0d", value);
        reset = 0;

        drive(0);
        drive(1);
        drive(1);
        drive(0);
        drive(1);
        drive(0);
        drive(0);
        drive(1);
        drive(1);
        drive(1);
        // Settle the final asserted cycle into the payload before checking it.
        drive(0);

        $display("PASS: counted %0d asserted cycles", expected);
        $finish;
    end
endmodule
