module integer_param_tb;
  logic clk = 0, reset = 1;
  logic signed [23:0] data_in = 0;
  logic data_in_valid = 0, data_in_ready;
  logic signed [47:0] product;
  logic product_valid, product_ready = 1;
  int seen = 0;
  mul dut(.clk(clk), .reset(reset), .data_in(data_in), .data_in_valid(data_in_valid),
    .data_in_ready(data_in_ready), .product(product), .product_valid(product_valid),
    .product_ready(product_ready));
  always #5 clk = ~clk;
  always_ff @(posedge clk) if (!reset && product_valid && product_ready) begin
    if (product !== data_in * 24'sd3) $fatal(1, "bad product %0d", product);
    seen <= seen + 1;
  end
  initial begin
    repeat (2) @(posedge clk);
    reset <= 0;
    data_in <= 24'sd7; data_in_valid <= 1;
    do @(posedge clk); while (!data_in_ready);
    @(negedge clk); data_in_valid <= 0;
    repeat (4) @(posedge clk);
    if (seen != 1) $fatal(1, "expected one product, got %0d", seen);
    $display("PASS integer parameter product %0d", product);
    $finish;
  end
endmodule
