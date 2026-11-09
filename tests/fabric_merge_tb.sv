module fabric_merge_tb;
  logic clk=0, reset=1;
  logic [7:0] s1=8'h11, s2=8'h22;
  logic [7:0] dst;
  logic dst_ready=1;
  logic s1_valid=1, s1_ready, s2_valid=0, s2_ready;
  int got=0, got2=0;
  top2 dut(.clk(clk), .reset(reset), .s1(s1), .s1_valid(s1_valid), .s1_ready(s1_ready),
    .s2(s2), .s2_valid(s2_valid), .s2_ready(s2_ready), .dst(dst));
  always #5 clk = ~clk;
  always_ff @(posedge clk) if (!reset && dut.mv && dut.mr) begin
    got <= got + 1;
    if (dut.m !== 8'h11 && dut.m !== 8'h22) $fatal(1,"bad payload %h",dut.m);
    if (!s1_valid) got2 <= got2 + 1;
  end
  initial begin
    repeat(2) @(posedge clk);
    reset <= 0;
    repeat(6) @(posedge clk);
    if (got == 0) $fatal(1,"no first-source packets");
    s1_valid <= 0; s2_valid <= 1; s2 <= 8'h22;
    repeat(8) @(posedge clk);
    if (got2 == 0) $fatal(1,"second source starved when first idle");
    $display("PASS merge routed %0d first and %0d second", got, got2);
    $finish;
  end
endmodule
