module validate_tb;
    logic clk = 0;
    logic reset = 1;
    logic seed = 0;
    logic kill = 0;
    logic [7:0] value;
    logic value_valid;
    logic value_ready = 0;
    logic [7:0] later;
    logic later_valid;
    logic later_ready = 1;

    validate_example dut (.*);
    always #5 clk = ~clk;

    initial begin
        $dumpfile("validate.vcd");
        $dumpvars(0, validate_tb);
        @(posedge clk); #1;
        if (!value_valid || value != 8'h00) $fatal(1, "reset validate did not seed a valid zero");
        @(posedge clk);
        reset <= 0;
        seed <= 1;
        value_ready <= 1;
        @(posedge clk); #1;
        if (!value_valid || value != 8'h5a) $fatal(1, "validate did not seed a valid buffer item: valid=%b value=%h", value_valid, value);
        if (!later_valid || later != 8'ha5) $fatal(1, "later transfer did not override invalidate");
        seed <= 0;
        kill <= 1;
        @(posedge clk); #1;
        if (value_valid) $fatal(1, "invalidate did not force invalid");
        kill <= 0;
        $display("validate PASS");
        $finish;
    end
endmodule
