module uart_tb;
    parameter integer DIV = 8;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [7:0] tx_data=0;
    reg tx_data_valid=0;
    wire tx_data_ready,tx;
    reg rx=1;
    wire [7:0] rx_data;
    wire rx_data_valid,framing_error,overrun;
    reg rx_data_ready=0;
    integer errors=0,overruns=0,received=0;
    integer trace_file,checks_file,trace_cycle=0;
    initial begin
        trace_file=$fopen("trace.csv","w");
        $fwrite(trace_file,"cycle,tx,data_valid,data_ready,data,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"direction,index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,uart_tb);
    end
    always @(posedge clk) begin
        trace_cycle=trace_cycle+1;
        if(trace_cycle<400) $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d\n",trace_cycle,tx,tx_data_valid,tx_data_ready,tx_data,reset);
    end
    reg [7:0] expected[0:255];
    pigen_uart_tx sender(
        .clk(clk),.reset(reset),.data(tx_data),.data_valid(tx_data_valid),.data_ready(tx_data_ready),.tx(tx));
    pigen_uart_rx receiver(
        .clk(clk),.reset(reset),.rx(rx),.data(rx_data),.data_valid(rx_data_valid),.data_ready(rx_data_ready),
        .framing_error(framing_error),.overrun(overrun));
    always @(posedge clk) if(!reset) begin
        if(framing_error) errors=errors+1;
        if(overrun) overruns=overruns+1;
        if(rx_data_valid && rx_data_ready) begin
            if(rx_data !== expected[received]) $fatal(1,"RX byte %0d got %h expected %h",received,rx_data,expected[received]);
            $fwrite(checks_file,"rx,%0d,%0d,%0d\n",received,expected[received],rx_data);
            received=received+1;
        end
    end
    task clocks(input integer n); repeat(n) @(negedge clk); endtask
    task send_tx(input reg[7:0] value);
        @(negedge clk); tx_data=value;tx_data_valid=1;
        do @(posedge clk); while(!tx_data_ready);
        @(negedge clk);tx_data_valid=0;
    endtask
    // Independent pin-level UART decoder checks bit order and stop duration.
    task check_tx(input reg[7:0] value);
        reg [7:0] observed;
        @(negedge tx);
        clocks(DIV/2);
        if(tx!==0) $fatal(1,"TX start bit");
        for(integer b=0;b<8;b=b+1) begin
            clocks(DIV);
            observed[b]=tx;
            if(tx!==value[b]) $fatal(1,"TX data %h bit %0d",value,b);
        end
        clocks(DIV);
        if(tx!==1) $fatal(1,"TX stop bit");
        $fwrite(checks_file,"tx,%0d,%0d,%0d\n",value,value,observed);
        clocks(DIV/2-1);
        if(tx!==1) $fatal(1,"TX short stop");
    endtask
    task send_rx(input reg[7:0] value,input bit bad_stop);
        @(negedge clk); rx=0;clocks(DIV);
        for(integer b=0;b<8;b=b+1) begin rx=value[b];clocks(DIV);end
        rx=!bad_stop;clocks(DIV);rx=1;clocks(DIV);
    endtask
    initial begin
        clocks(4);reset=0;clocks(3);
        if(tx!==1) $fatal(1,"TX idle level");
        // Exhaustive byte patterns on TX, with bubbles in the input stream.
        for(integer k=0;k<256;k=k+1) begin
            fork send_tx(k[7:0]^8'ha5);check_tx(k[7:0]^8'ha5);join
            clocks(k%3);
        end
        rx_data_ready=1;
        // Exhaustive RX patterns, driven independently of the transmitter.
        for(integer k=0;k<256;k=k+1) begin
            expected[k]=k[7:0];send_rx(k[7:0],0);
        end
        clocks(4);
        if(received!=256 || errors || overruns) $fatal(1,"RX exhaustive counters %0d/%0d/%0d",received,errors,overruns);
        // Short false start must not produce a byte.
        rx=0;clocks(1);rx=1;clocks(12*DIV);
        if(rx_data_valid || errors) $fatal(1,"false start accepted");
        send_rx(8'h55,1);clocks(4);
        if(errors!=1 || rx_data_valid) $fatal(1,"framing error not rejected");
        // Keep the old buffered byte stable while a later frame overruns.
        rx_data_ready=0;
        send_rx(8'ha6,0);clocks(4);
        if(!rx_data_valid || rx_data!==8'ha6) $fatal(1,"missing buffered byte");
        send_rx(8'h3c,0);clocks(4);
        if(overruns!=1 || rx_data!==8'ha6 || !rx_data_valid) $fatal(1,"overrun corrupted old byte");
        // Reset aborts a partial frame and clears a stalled output.
        rx=0;clocks(DIV*3);reset=1;rx=1;clocks(4);reset=0;clocks(12*DIV);
        if(rx_data_valid || tx!==1) $fatal(1,"reset did not flush UART");
        $display("PASS uart DIV=%0d exhaustive TX/RX, false start, framing, overrun, reset",DIV);$finish;
    end
    initial begin #2000000;$fatal(1,"UART timeout");end
endmodule
