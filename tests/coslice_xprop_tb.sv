module coslice_xprop_tb;
    logic clk=0,reset=1;
    logic [7:0] source=8'h3c,left,previous;
    logic source_valid=0,source_ready,left_valid,left_ready=1;
    coslice_xprop dut(.*);
    always #5 clk=~clk;
    initial begin
        repeat(2)@(negedge clk);reset=0;source_valid=1;
        @(posedge clk);#1;
        if(!left_valid || left!==8'h3d)$fatal(1,"co-slice payload mismatch %h",left);
        if(previous!==8'hxx)$fatal(1,"first previous must retain pre-edge unknown state %h",previous);
        @(negedge clk);source=8'h7e;
        @(posedge clk);#1;
        if(!left_valid || left!==8'h7f || previous!==8'h3c)$fatal(1,"second co-slice/old state %h %h",left,previous);
        @(negedge clk);left_ready=0;source=8'ha1;
        repeat(3)begin
            @(posedge clk);#1;
            if(left!==8'h7f || previous!==8'h3c)$fatal(1,"co-slice moved during stall");
        end
        @(negedge clk);left_ready=1;
        @(posedge clk);#1;
        if(left!==8'ha2 || previous!==8'h7e)$fatal(1,"co-slice resume %h %h",left,previous);
        $display("PASS: co-slice total-width truncation, pre-edge unknown state and stalls");$finish;
    end
    initial begin #1000;$fatal(1,"co-slice timeout");end
endmodule
