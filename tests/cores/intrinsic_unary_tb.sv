module intrinsic_unary_tb;
    reg clk=0,reset=1,source_valid=0,destination_ready=0;
    reg [7:0] source=0;
    wire source_ready,destination_valid,missing,blocked,parity,all_bits,no_bits;
    wire [7:0] destination,inverted;
    intrinsic_unary dut(.*);
    always #5 clk=~clk;
    reg exp_missing,exp_blocked,exp_parity,exp_all,exp_none;
    reg[7:0] exp_inverse;
    always @(posedge clk) if(!reset) begin
        exp_missing=!source_valid;
        exp_blocked=destination_valid && !destination_ready;
        exp_parity=^source;exp_all=&source;exp_none=~|source;exp_inverse=~source;
        #1;
        if({missing,blocked,parity,all_bits,no_bits,inverted} !==
           {exp_missing,exp_blocked,exp_parity,exp_all,exp_none,exp_inverse}) $fatal(1,"unary intrinsic semantics");
    end
    initial begin
        repeat(4) @(negedge clk);reset=0;
        for(integer k=0;k<1024;k=k+1) begin
            source=k[7:0];source_valid=k[0];destination_ready=k[2];@(negedge clk);
        end
        $display("PASS intrinsic unary: negated valid/ready, complement, XOR/AND/NOR across 1024 inputs");$finish;
    end
    initial begin #20000;$fatal(1,"unary timeout");end
endmodule
