module fabric_block_tb;
    logic clk = 0, reset = 1, enable = 1;
    logic direct_src__tx__direct__valid = 0, direct_src__tx__direct__ready;
    logic [7:0] direct_src__tx__direct__payload = 0;
    logic direct_dst__rx__valid, direct_dst__rx__ready = 0;
    logic [7:0] direct_dst__rx__payload;
    logic source_a__tx__to_sink__valid = 0, source_a__tx__to_sink__ready;
    logic [7:0] source_a__tx__to_sink__payload = 0;
    logic source_d__tx__to_sink__valid = 0, source_d__tx__to_sink__ready;
    logic [7:0] source_d__tx__to_sink__payload = 0;
    logic sink__rx__valid, sink__rx__ready = 1;
    logic [7:0] sink__rx__payload;
    logic [1:0] sink__rx__path;
    native_fabric dut (.*);
    always #5 clk = ~clk;

    task automatic send_direct(input logic [7:0] value);
        @(negedge clk); direct_src__tx__direct__payload = value; direct_src__tx__direct__valid = 1;
        do @(posedge clk); while (!direct_src__tx__direct__ready);
        @(negedge clk); direct_src__tx__direct__valid = 0;
    endtask
    task automatic send_a(input logic [7:0] value);
        @(negedge clk); source_a__tx__to_sink__payload = value; source_a__tx__to_sink__valid = 1;
        do @(posedge clk); while (!source_a__tx__to_sink__ready);
        @(negedge clk); source_a__tx__to_sink__valid = 0;
    endtask
    task automatic send_d(input logic [7:0] value);
        @(negedge clk); source_d__tx__to_sink__payload = value; source_d__tx__to_sink__valid = 1;
        do @(posedge clk); while (!source_d__tx__to_sink__ready);
        @(negedge clk); source_d__tx__to_sink__valid = 0;
    endtask
    initial begin
        repeat (2) @(posedge clk); reset = 0;
        send_direct(8'h5a); wait (direct_dst__rx__valid);
        if (direct_dst__rx__payload !== 8'h5a) $fatal(1, "direct fabric delivery failed");
        repeat (2) begin
            @(posedge clk); #1;
            if (!direct_dst__rx__valid || direct_dst__rx__payload !== 8'h5a)
                $fatal(1, "direct fabric output did not hold under backpressure");
        end
        @(negedge clk); direct_dst__rx__ready = 1;
        @(posedge clk); @(negedge clk);
        send_a(8'h31); wait (sink__rx__valid);
        if (sink__rx__payload !== 8'h31 || sink__rx__path !== dut.sink__rx__SOURCE__a)
            $fatal(1, "first routed fabric delivery failed");
        @(posedge clk); @(negedge clk);
        send_d(8'hd4); wait (sink__rx__valid);
        if (sink__rx__payload !== 8'hd4 || sink__rx__path !== dut.sink__rx__SOURCE__d)
            $fatal(1, "second routed fabric delivery failed");
        $display("PASS: native fabric language block");
        $finish;
    end
endmodule
