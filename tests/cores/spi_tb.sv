module spi_tb;
    parameter integer HALF=4, POL=0, PHA=0;
    reg clk=0, reset=1;
    always #5 clk=~clk;
    reg [7:0] command=0;
    reg command_valid=0,response_ready=0,miso=0;
    wire command_ready,response_valid,mosi,sck,cs_n;
    wire [7:0] response;
    reg [7:0] sent,reply,captured;
    integer edges=0,samples=0,drive_index=7,transactions=0;
    time last_edge;
    integer trace_file,checks_file,trace_cycle=0;
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,sck,cs_n,mosi,miso,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,tx_expected,tx_observed,rx_expected,rx_observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,spi_tb);
    end
    always @(negedge clk) begin
        trace_cycle=trace_cycle+1;
        if(trace_cycle<500) $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d\n",trace_cycle,sck,cs_n,mosi,miso,reset);
    end
    pigen_spi_controller dut(.*);
    always @(negedge cs_n) if(!reset) begin
        edges=0;samples=0;captured=0;drive_index=7;last_edge=$time;
        if(!PHA) begin miso=reply[7];drive_index=6;end
        if(sck!==POL[0]) $fatal(1,"SPI wrong idle polarity");
    end
    always @(sck) if(!reset && !cs_n) begin
        if($time-last_edge!=HALF*10) $fatal(1,"SPI half-period %0t expected %0d",$time-last_edge,HALF*10);
        last_edge=$time;edges=edges+1;
        if((sck!=POL[0] && !PHA) || (sck==POL[0] && PHA)) begin
            captured={captured[6:0],mosi};samples=samples+1;
        end else if(drive_index>=0) begin
            miso=reply[drive_index];drive_index=drive_index-1;
        end
    end
    always @(posedge cs_n) if(!reset) begin
        if(edges!=16 || samples!=8 || captured!==sent)
            $fatal(1,"SPI transaction edges=%0d samples=%0d data=%h expected=%h",edges,samples,captured,sent);
        transactions=transactions+1;
    end
    initial begin
        repeat(4) @(negedge clk);reset=0;repeat(3) @(negedge clk);
        for(integer k=0;k<64;k=k+1) begin
            sent=(k*73)^8'ha5;reply=(k*29)^8'h3c;
            command=sent;command_valid=1;
            do @(posedge clk); while(!command_ready);
            @(negedge clk);command_valid=0;
            wait(response_valid);@(negedge clk);
            repeat(k%11) begin
                if(!response_valid || response!==reply) $fatal(1,"SPI stalled response changed");
                @(negedge clk);
            end
            if(response!==reply) $fatal(1,"SPI received %h expected %h",response,reply);
            $fwrite(checks_file,"%0d,%0d,%0d,%0d,%0d\n",k,sent,captured,reply,response);
            response_ready=1;@(negedge clk);response_ready=0;
        end
        if(transactions!=64) $fatal(1,"SPI missing transactions");
        // Reset in the middle of the next transaction must release select/clock.
        command_valid=1;wait(!cs_n);repeat(3) @(negedge clk);reset=1;
        repeat(3) @(negedge clk);
        if(!cs_n || sck!==POL[0] || response_valid) $fatal(1,"SPI reset abort");
        $display("PASS spi mode=%0d HALF=%0d 64 full-duplex transactions, stalls, reset",POL*2+PHA,HALF);$finish;
    end
    initial begin #2000000;$fatal(1,"SPI timeout");end
endmodule
