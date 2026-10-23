module signed_widen_tb;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic signed [23:0] lhs = '0;
    logic lhs_valid = 1'b0;
    logic lhs_ready;
    logic signed [23:0] rhs = '0;
    logic rhs_valid = 1'b0;
    logic rhs_ready;
    logic signed [49:0] product;
    logic product_valid;
    logic product_ready = 1'b1;

    signed_widen dut (.*);

    always #5 clk = ~clk;

    initial begin
        #17 reset = 1'b0;
        @(negedge clk);
        lhs = -24'sd6710886;
        rhs = 24'sd191477;
        lhs_valid = 1'b1;
        rhs_valid = 1'b1;
        do @(negedge clk); while (!(lhs_ready && rhs_ready));
        lhs_valid = 1'b0;
        rhs_valid = 1'b0;
        do @(posedge clk); while (!product_valid);
        if (product !== -50'sd1284980318622)
            $fatal(1, "signed widening failed: got %0d", product);
        if (product[49:42] !== 8'hff)
            $fatal(1, "negative product was not sign-extended: high bits=%h", product[49:42]);
        $display("PASS: signed 24x24 product sign-extended to 50 bits");
        $finish;
    end
endmodule
