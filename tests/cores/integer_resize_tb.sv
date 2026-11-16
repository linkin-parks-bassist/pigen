module integer_resize_tb;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [15:0] value=0;
    reg value_valid=0,result_ready=0;
    wire value_ready,result_valid;
    wire [127:0] result;
    integer_resize dut(.*);
    reg [127:0] queue[0:65535],held;
    reg stalled=0;
    integer sent=0,received=0,cycle=0,trace_file,checks_file;
    reg accepted=0;
    function [63:0] math_expected(input [7:0] v);
        reg [15:0] copy,shifted,added,product;
        begin copy={8'b0,v};shifted={4'b0,v,4'b0};added={8'b0,v}+16'd1;product={8'b0,v}*{8'b0,v};
            math_expected={copy,shifted,added,product};end
    endfunction
    always @(posedge clk)begin
        cycle=cycle+1;
        if(!reset)begin
            if(stalled && (!result_valid || result!==held))$fatal(1,"resize stalled result changed");
            accepted=value_valid && value_ready;
            if(accepted)begin
                queue[sent]={value[7:0],8'b0,value,value[7:0],{8{value[15]}},value,math_expected(value[7:0])};sent=sent+1;
            end
            if(result_valid && result_ready)begin
                if(received>=sent || result!==queue[received])$fatal(1,"resize mismatch %0d %h %h",received,result,queue[received]);
                $fwrite(checks_file,"%0d,%0d,%0d\n",received,queue[received],result);received=received+1;
            end
            stalled=result_valid && !result_ready;held=result;
        end
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d\n",cycle,value_valid,value_ready,result_valid,result_ready);
    end
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,in_valid,in_ready,out_valid,out_ready\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,integer_resize_tb);
        repeat(3)@(negedge clk);reset=0;value_valid=1;
        while(sent<65536)begin
            @(negedge clk);
            if(accepted)begin value=16'(sent);value_valid=sent<65536;end
            result_ready=cycle%13>3;
        end
        @(negedge clk);value_valid=0;result_ready=1;
        wait(received==sent);@(negedge clk);
        $display("PASS integer resize: all 65536 unsigned/signed narrow/widen patterns, concatenated results and stalls");$finish;
    end
    initial begin #2000000;$fatal(1,"resize timeout");end
endmodule
