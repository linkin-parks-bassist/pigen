module random_fabric_block_tb;
    logic clk = 0, reset = 1, enable = 1;
    logic a__tx__sink__valid = 0, a__tx__sink__ready;
    logic [7:0] a__tx__sink__payload = 0;
    logic d__tx__sink__valid = 0, d__tx__sink__ready;
    logic [7:0] d__tx__sink__payload = 0;
    logic sink__rx__valid, sink__rx__ready = 0;
    logic [7:0] sink__rx__payload;
    logic [1:0] sink__rx__path;
    integer sent_a = 0, sent_d = 0, received = 0;
    logic [31:0] seen = 0;
    random_fabric dut (.*);
    always #5 clk = ~clk;
    always @(negedge clk) begin
        if (!reset) begin
            sink__rx__ready = $urandom_range(0, 1);
            if (!a__tx__sink__valid && sent_a < 16 && $urandom_range(0, 1)) begin
                a__tx__sink__payload = sent_a; a__tx__sink__valid = 1;
            end
            if (!d__tx__sink__valid && sent_d < 16 && $urandom_range(0, 1)) begin
                d__tx__sink__payload = 8'd128 + sent_d; d__tx__sink__valid = 1;
            end
        end
    end
    always @(posedge clk) begin
        integer index;
        if (!reset) begin
            if (a__tx__sink__valid && a__tx__sink__ready) begin sent_a <= sent_a + 1; a__tx__sink__valid <= 0; end
            if (d__tx__sink__valid && d__tx__sink__ready) begin sent_d <= sent_d + 1; d__tx__sink__valid <= 0; end
            if (sink__rx__valid && sink__rx__ready) begin
                if (sink__rx__payload < 16) index = sink__rx__payload;
                else if (sink__rx__payload >= 128 && sink__rx__payload < 144) index = 16 + sink__rx__payload - 128;
                else $fatal(1, "unexpected payload %0d", sink__rx__payload);
                if (seen[index]) $fatal(1, "duplicate payload %0d", sink__rx__payload);
                seen[index] <= 1'b1; received <= received + 1;
            end
        end
    end
    initial begin
        repeat (2) @(posedge clk); reset = 0;
        repeat (1000) begin
            @(posedge clk);
            if (received == 32) begin
                if (seen !== 32'hffff_ffff) $fatal(1, "missing payloads");
                $display("PASS: randomized native fabric backpressure");
                $finish;
            end
        end
        $fatal(1, "randomized fabric timed out: received %0d", received);
    end
endmodule
