`timescale 1ns/1ps
module spi_target_tb;
    parameter integer POL=0,PHA=0,HALF=73;
    reg clk=0,reset=1,cs_n=1,sck=POL,mosi=0;
    always #5 clk=~clk;
    reg [7:0] tx=0;
    reg tx_valid=0,rx_ready=0;
    wire tx_ready,rx_valid,miso,miso_enable,underrun,overrun;
    wire [7:0] rx;
    pigen_spi_target dut(.*);
    integer checks,trace,cycle=0,received=0,expected_count=0,under_count=0,over_count=0;
    reg [7:0] expected[0:1023];
    reg [7:0] captured;
    initial begin
        checks=$fopen("checks.csv","w");$fwrite(checks,"index,expected,observed\n");
        trace=$fopen("trace.csv","w");$fwrite(trace,"cycle,cs_n,sck,mosi,miso,miso_enable,tx_valid,tx_ready,rx_valid,rx_ready,underrun,overrun\n");
        $dumpfile("wave.vcd");$dumpvars(0,spi_target_tb);
    end
    always @(posedge clk) if(!reset) begin
        if(underrun) under_count=under_count+1;
        if(overrun) over_count=over_count+1;
        if(rx_valid && rx_ready) begin
            if(received>=expected_count || rx!==expected[received])
                $fatal(1,"SPI target RX index=%0d got=%h expected=%h queued=%0d",received,rx,expected[received],expected_count);
            $fwrite(checks,"%0d,%0d,%0d\n",received,expected[received],rx);received=received+1;
        end
    end
    always @(negedge clk) begin
        cycle=cycle+1;
        if(cycle<900) $fwrite(trace,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,cs_n,sck,mosi,miso,miso_enable,tx_valid,tx_ready,rx_valid,rx_ready,underrun,overrun);
        if(!reset && cs_n && miso_enable) $fatal(1,"MISO must release when deselected");
    end
    task load_tx(input [7:0] value);
        begin
            @(negedge clk);tx=value;tx_valid=1;
            do @(posedge clk);while(!tx_ready);
            @(negedge clk);tx_valid=0;
        end
    endtask
    task select_target;
        begin #17;cs_n=0;#(HALF+13);if(!miso_enable) $fatal(1,"missing MISO enable");end
    endtask
    task deselect_target;
        begin #19;cs_n=1;#1;if(miso_enable) $fatal(1,"late MISO disable");#(HALF+21);end
    endtask
    task byte_on_pins(input [7:0] outgoing,input [7:0] reply,input integer keep_rx);
        begin
            if(keep_rx) begin expected[expected_count]=outgoing;expected_count=expected_count+1;end
            captured=0;
            for(integer bitno=7;bitno>=0;bitno=bitno-1) begin
                if(!PHA) mosi=outgoing[bitno];
                // Non-integral relation to the system clock, with deterministic jitter.
                #(HALF+(bitno%3));sck=!POL;
                if(PHA) mosi=outgoing[bitno];
                else captured={captured[6:0],miso};
                #40;
                if(!PHA && miso!==captured[0]) $fatal(1,"MISO changed after sample before shift edge");
                #(HALF+((bitno+1)%3)-40);sck=POL;
                if(PHA) captured={captured[6:0],miso};
            end
            if(captured!==reply) $fatal(1,"SPI target TX got=%h expected=%h mode=%0d",captured,reply,2*POL+PHA);
            $fwrite(checks,"%0d,%0d,%0d\n",1000+expected_count,reply,captured);
            #41;
        end
    endtask
    initial begin : stimulus
        integer u,o,prior;
        repeat(5) @(negedge clk);reset=0;repeat(5) @(negedge clk);rx_ready=1;
        // All byte values in both directions, continuous CS across byte boundaries.
        load_tx(8'ha5);select_target();
        for(integer n=0;n<256;n=n+1) begin
            if(n!=0) load_tx((n*73)^8'ha5);
            byte_on_pins(n[7:0],(n*73)^8'ha5,1);
        end
        deselect_target();repeat(10) @(negedge clk);
        if(received!=256 || under_count || over_count) $fatal(1,"normal stream counters %0d %0d %0d",received,under_count,over_count);
        // No TX token: entire bytes are zero-filled, with one pulse per byte.
        u=under_count;select_target();byte_on_pins(8'h91,0,1);byte_on_pins(8'h2e,0,1);deselect_target();
        if(under_count-u!=2) $fatal(1,"underrun pulse count");
        // A token arriving mid-byte is reserved for the following byte.
        select_target();
        fork
            byte_on_pins(8'h81,0,1);
            begin #(HALF*5);load_tx(8'hb9);end
        join
        byte_on_pins(8'h42,8'hb9,1);deselect_target();
        // Backpressure cannot stop the external clock. Retain old RX, drop newest.
        rx_ready=0;o=over_count;select_target();byte_on_pins(8'hc7,0,1);byte_on_pins(8'h68,0,0);
        if(!rx_valid || rx!==8'hc7) $fatal(1,"held RX changed");
        byte_on_pins(8'hf1,0,0);deselect_target();
        if(over_count-o!=2 || rx!==8'hc7) $fatal(1,"overrun must drop newest");
        @(negedge clk);rx_ready=1;repeat(6) @(negedge clk);
        // Abort after each possible incomplete bit count, then prove resynchronization.
        for(integer bits=1;bits<8;bits=bits+1) begin
            prior=received;load_tx(8'hdd);select_target();
            repeat(bits) begin mosi=1;#HALF;sck=!POL;#HALF;sck=POL;end
            #41;load_tx(8'h36);
            deselect_target();repeat(6) @(negedge clk);
            if(received!=prior || rx_valid) $fatal(1,"partial byte published");
            select_target();byte_on_pins(8'h49,8'h36,1);deselect_target();
        end
        // Reset while selected flushes traffic and disables MISO immediately.
        load_tx(8'hb6);select_target();mosi=1;#HALF;sck=!POL;#HALF;sck=POL;
        #3;reset=1;#1;if(miso_enable) $fatal(1,"reset MISO enable");
        repeat(5) @(negedge clk);cs_n=1;sck=POL;reset=0;repeat(6) @(negedge clk);
        load_tx(8'h69);select_target();byte_on_pins(8'h96,8'h69,1);deselect_target();
        repeat(8) @(negedge clk);
        if(received!=expected_count || rx_valid) $fatal(1,"missing or extra RX bytes %0d/%0d",received,expected_count);
        $display("PASS spi target mode=%0d HALF=%0dns 256 full-duplex bytes, continuous CS, underrun/overrun, seven abort positions, reset; RX=%0d",2*POL+PHA,HALF,received);$finish;
    end
    initial begin #2000000;$fatal(1,"SPI target timeout");end
endmodule
