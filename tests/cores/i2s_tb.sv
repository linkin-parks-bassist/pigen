module i2s_tb;
    parameter HALF=4;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [31:0] samples=32'h1234abcd;
    reg samples_valid=1,received_ready=1,serial_in=0;
    wire samples_ready,received_valid,serial_out,bclk,word_select,underrun,overrun;
    wire [31:0] received;
    pigen_i2s_controller dut(.*);
    reg [31:0] incoming=32'hca53f081,captured=0,wire_expected=0,next_word;
    reg accepted_edge=0,receive_admitted=0;
    integer bit_index=0,frames=0,got=0,tx_count=0,dropped=0,empty=0;
    reg [31:0] expected[0:255];
    integer read_ptr=0,write_ptr=0;
    integer trace_file,checks_file,cycle=0;
    initial begin
        serial_in=incoming[31];
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,bclk,ws,tx,rx,underrun,overrun,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"direction,index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,i2s_tb);
    end
    always @(posedge clk) begin
        receive_admitted=!received_valid || received_ready;
        accepted_edge=!reset && samples_valid && samples_ready;
        if(accepted_edge) begin next_word=samples;tx_count=tx_count+1;end
        if(!reset && received_valid && received_ready) begin
            if(read_ptr>=write_ptr || received!==expected[read_ptr]) $fatal(1,"I2S RX %h expected %h",received,expected[read_ptr]);
            $fwrite(checks_file,"rx,%0d,%0d,%0d\n",got,expected[read_ptr],received);
            read_ptr=read_ptr+1;got=got+1;
        end
        if(!reset && overrun) dropped=dropped+1;
        if(!reset && underrun) empty=empty+1;
    end
    // Independent external source and decoder, referenced to the physical BCLK.
    always @(negedge bclk) if(!reset) serial_in=incoming[31-bit_index];
    always @(posedge bclk) if(!reset) begin
        if(word_select!==((bit_index>=15 && bit_index<31)?1'b1:1'b0)) $fatal(1,"I2S WS lead wrong at slot %0d",bit_index);
        captured={captured[30:0],serial_out};
        if(bit_index==31) begin
            if(captured!==wire_expected) $fatal(1,"I2S TX frame %0d %h expected %h",frames,captured,wire_expected);
            $fwrite(checks_file,"tx,%0d,%0d,%0d\n",frames,wire_expected,captured);
            wire_expected=accepted_edge ? next_word : 0;
            // Output storage accepts if empty or being consumed on this edge.
            if(receive_admitted) begin expected[write_ptr]=incoming;write_ptr=write_ptr+1;end
            incoming=incoming*1664525+1013904223;
            frames=frames+1;bit_index=0;
        end else bit_index=bit_index+1;
    end
    always @(negedge clk) begin
        cycle=cycle+1;
        if(cycle<1800) $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,bclk,word_select,serial_out,serial_in,underrun,overrun,reset);
        if(accepted_edge) samples=samples*1103515245+12345;
    end
    initial begin
        repeat(4) @(negedge clk);reset=0;
        wait(frames==12);@(negedge clk);samples_valid=0;received_ready=0;
        wait(frames==18);@(negedge clk);samples_valid=1;received_ready=1;
        wait(frames==40);repeat(4) @(negedge clk);
        if(got<30 || dropped<4 || empty<4 || tx_count<30) $fatal(1,"I2S insufficient coverage %0d/%0d/%0d/%0d",got,dropped,empty,tx_count);
        reset=1;repeat(3) @(negedge clk);
        if(bclk || word_select || serial_out || received_valid) $fatal(1,"I2S reset");
        $display("PASS i2s HALF=%0d 40 stereo frames, independent TX/RX, WS lead, silence/drop, reset",HALF);$finish;
    end
    initial begin #2000000;$fatal(1,"I2S timeout");end
endmodule
