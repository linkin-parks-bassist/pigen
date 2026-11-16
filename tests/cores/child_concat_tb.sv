module child_concat_tb;
    reg clk=0,reset=1,value_valid=0,result_ready=0;
    reg [7:0] value=0;
    wire value_ready,result_valid;
    wire [3:0] upper,lower;
    integer sent=0,received=0;
    reg [7:0] expected[0:255],held;
    reg stalled=0,accepted=0;
    concat_wrapper dut(.*);
    always #5 clk=~clk;
    always @(posedge clk) if(!reset) begin
        accepted=value_valid && value_ready;
        if(accepted) begin expected[sent]=value;sent=sent+1;end
        if(stalled && (!result_valid || {upper,lower}!==held)) $fatal(1,"concat stalled output changed");
        if(result_valid && result_ready) begin
            if(received>=sent || {upper,lower}!==expected[received]) $fatal(1,"concat corrupt byte %0d",received);
            received=received+1;
        end
        stalled=result_valid && !result_ready;held={upper,lower};
    end
    initial begin
        repeat(4) @(negedge clk);reset=0;
        for(integer k=0;k<500;k=k+1) begin
            if(!value_valid || accepted) begin value_valid=sent<200;value=sent^8'ha5;end
            result_ready=k%7>2;
            @(negedge clk);
        end
        value_valid=0;result_ready=1;repeat(4) @(negedge clk);
        if(sent!=200 || received!=sent) $fatal(1,"concat count %0d/%0d",sent,received);
        $display("PASS child output concatenation: 200 beats, stalls and read-only destination rejection");$finish;
    end
    initial begin #10000;$fatal(1,"concat timeout");end
endmodule
