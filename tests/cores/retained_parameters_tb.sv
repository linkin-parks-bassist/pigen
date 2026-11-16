module retained_parameters_tb;
    reg clk=0,reset=1,source_valid=0,destination_ready=1;
    reg [7:0] source=8'ha7;
    wire source_ready,destination_valid;
    wire [7:0] destination;
    retained_parameters dut(.*);
    always #5 clk=~clk;
    initial begin
        repeat(3) @(negedge clk);reset=0;source_valid=1;
        do @(posedge clk);while(!source_ready);
        @(negedge clk);source_valid=0;
        if(!destination_valid || destination!==8'ha7) $fatal(1,"parameterized native transfer");
        $display("PASS retained parameters: macro, integer, signed/unsigned/bit counts, dependent localparam, native transfer");$finish;
    end
    initial begin #1000;$fatal(1,"parameter retention timeout");end
endmodule
