`timescale 1ns/1ps
module uart_framing_tb;
    parameter integer DIV=7,BITS=9,MODE=2,STOPS=2;
    localparam integer COUNT=1<<BITS,PERIOD=DIV*10;
    reg clk=0,reset=1,rx=1;
    always #5 clk=~clk;
    reg [BITS-1:0] tx_data=0;
    reg tx_data_valid=0,rx_data_ready=0;
    wire tx,tx_data_ready,rx_data_valid,framing_error,parity_error,overrun;
    wire [BITS-1:0] rx_data;
    pigen_uart_tx sender(.clk(clk),.reset(reset),.data(tx_data),.data_valid(tx_data_valid),.data_ready(tx_data_ready),.tx(tx));
    pigen_uart_rx receiver(.clk(clk),.reset(reset),.rx(rx),.data(rx_data),.data_valid(rx_data_valid),.data_ready(rx_data_ready),.framing_error(framing_error),.parity_error(parity_error),.overrun(overrun));
    reg [BITS-1:0] expected[0:2047],held;
    integer queued=0,received=0,framing_errors=0,parity_errors=0,overruns=0,checks,trace,cycle=0;
    reg stalled=0,checking_stop=0;
    real stop_deadline=0;
    function automatic bit parity_of(input reg[BITS-1:0] value);
        integer ones;
        begin ones=0;for(integer b=0;b<BITS;b=b+1)ones=ones+value[b];parity_of=(ones%2)^(MODE==2);end
    endfunction
    initial begin
        checks=$fopen("checks.csv","w");$fwrite(checks,"direction,index,expected,observed\n");
        trace=$fopen("trace.csv","w");$fwrite(trace,"cycle,tx,rx,tx_valid,tx_ready,rx_valid,rx_ready,framing_error,parity_error,overrun,reset\n");
        $dumpfile("wave.vcd");$dumpvars(0,uart_framing_tb);
    end
    always @(posedge clk)begin
        cycle=cycle+1;
        if(cycle<1000)$fwrite(trace,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,tx,rx,tx_data_valid,tx_data_ready,rx_data_valid,rx_data_ready,framing_error,parity_error,overrun,reset);
        if(reset)stalled=0;
        else begin
            if(framing_error)framing_errors=framing_errors+1;
            if(parity_error)parity_errors=parity_errors+1;
            if(overrun)overruns=overruns+1;
            if(stalled&&(!rx_data_valid||rx_data!==held))$fatal(1,"UART held output changed");
            if(rx_data_valid&&rx_data_ready)begin
                if(received>=queued||rx_data!==expected[received])$fatal(1,"UART RX index=%0d got=%h expected=%h",received,rx_data,expected[received]);
                $fwrite(checks,"rx,%0d,%0d,%0d\n",received,expected[received],rx_data);received=received+1;
            end
            stalled=rx_data_valid&&!rx_data_ready;held=rx_data;
        end
    end
    always @(tx)if(checking_stop&&tx===0&&$realtime<stop_deadline)$fatal(1,"UART shortened stop interval");
    task send_tx(input reg[BITS-1:0] value);
        begin @(negedge clk);tx_data=value;tx_data_valid=1;do @(posedge clk);while(!tx_data_ready);@(negedge clk);tx_data_valid=0;end
    endtask
    task check_tx(input reg[BITS-1:0] value);
        reg[BITS-1:0] observed;
        begin
            @(negedge tx);#(PERIOD/2.0);
            if(tx!==0)$fatal(1,"UART missing start bit");
            for(integer b=0;b<BITS;b=b+1)begin #PERIOD;observed[b]=tx;if(tx!==value[b])$fatal(1,"UART TX data bit=%0d",b);end
            if(MODE!=0)begin #PERIOD;if(tx!==parity_of(value))$fatal(1,"UART TX parity mode=%0d value=%h got=%b",MODE,value,tx);$fwrite(checks,"tx_parity,%0d,%0d,%0d\n",value,parity_of(value),tx);end
            #(PERIOD/2.0+0.001);if(tx!==1)$fatal(1,"UART stop bit level");
            checking_stop=1;stop_deadline=$realtime+STOPS*PERIOD-0.002;
            #(STOPS*PERIOD-0.002);checking_stop=0;
            $fwrite(checks,"tx,%0d,%0d,%0d\n",value,value,observed);
        end
    endtask
    task queue_rx(input reg[BITS-1:0] value);
        begin expected[queued]=value;queued=queued+1;end
    endtask
    // No inserted idle bit: consecutive calls produce true back-to-back frames.
    task drive_rx(input reg[BITS-1:0] value,input bit bad_parity,input integer bad_stop);
        begin
            rx=0;#PERIOD;
            for(integer b=0;b<BITS;b=b+1)begin rx=value[b];#PERIOD;end
            if(MODE!=0)begin rx=parity_of(value)^bad_parity;#PERIOD;end
            for(integer b=0;b<STOPS;b=b+1)begin rx=(b!=bad_stop);#PERIOD;end
        end
    endtask
    initial begin : stimulus
        integer old_rx,old_error,old_parity,old_overrun;
        repeat(5)@(negedge clk);reset=0;repeat(4)@(negedge clk);
        fork
            for(integer n=0;n<COUNT;n=n+1)send_tx(n[BITS-1:0]^{BITS{1'b1}});
            for(integer n=0;n<COUNT;n=n+1)check_tx(n[BITS-1:0]^{BITS{1'b1}});
        join
        @(negedge clk);rx_data_ready=1;#2;
        for(integer n=0;n<COUNT;n=n+1)begin queue_rx(n[BITS-1:0]);drive_rx(n[BITS-1:0],0,-1);end
        #(4*PERIOD);
        if(received!=COUNT||framing_errors||parity_errors||overruns)$fatal(1,"UART exhaustive stream counters RX=%0d/%0d frame=%0d parity=%0d overrun=%0d",received,COUNT,framing_errors,parity_errors,overruns);
        // A one-clock false start must not publish anything.
        old_rx=received;rx=0;#10;rx=1;#(3*PERIOD);
        if(received!=old_rx||rx_data_valid||framing_errors||parity_errors)$fatal(1,"UART false start accepted");
        if(MODE!=0)begin
            drive_rx('h5a,1,-1);queue_rx('h83);drive_rx('h83,0,-1);#(3*PERIOD);
            if(parity_errors!=1||framing_errors||received!=old_rx+1)$fatal(1,"UART bad parity rejection/recovery");
        end
        // Validate each configured stop position, including the second stop.
        for(integer stop=0;stop<STOPS;stop=stop+1)begin
            old_rx=received;old_error=framing_errors;old_parity=parity_errors;
            drive_rx('h55,0,stop);rx=1;#(3*PERIOD);
            if(framing_errors!=old_error+1||parity_errors!=old_parity||received!=old_rx||rx_data_valid)$fatal(1,"UART bad stop %0d accepted or repeated",stop);
            queue_rx('h96);drive_rx('h96,0,-1);#(2*PERIOD);
        end
        @(negedge clk);rx_data_ready=0;queue_rx('ha6);drive_rx('ha6,0,-1);#(2*PERIOD);
        if(!rx_data_valid||rx_data!==BITS'('ha6))$fatal(1,"UART missing held word");
        old_overrun=overruns;drive_rx('h3c,0,-1);#(2*PERIOD);
        if(overruns!=old_overrun+1||rx_data!==BITS'('ha6))$fatal(1,"UART overrun replaced old word");
        if(MODE!=0)begin old_parity=parity_errors;drive_rx('h7e,1,-1);#(2*PERIOD);if(parity_errors!=old_parity+1||overruns!=old_overrun+1||rx_data!==BITS'('ha6))$fatal(1,"UART corrupt full-buffer frame");end
        @(negedge clk);rx_data_ready=1;#(2*PERIOD);
        // A held-low break causes one frame error, then waits for idle recovery.
        old_error=framing_errors;old_parity=parity_errors;old_rx=received;
        rx=0;#((BITS+STOPS+8)*PERIOD);
        if(framing_errors!=old_error+1||received!=old_rx||rx_data_valid||parity_errors!=old_parity+(MODE==2))$fatal(1,"UART break handling");
        rx=1;#(3*PERIOD);queue_rx('h69);drive_rx('h69,0,-1);#(2*PERIOD);
        // Reset clears a held result and aborts both serializers mid-frame.
        @(negedge clk);rx_data_ready=0;queue_rx('hd3);drive_rx('hd3,0,-1);#(2*PERIOD);
        send_tx('hff);wait(!tx);rx=0;#(3*PERIOD);@(negedge clk);reset=1;rx=1;queued=received;
        repeat(6)@(negedge clk);if(tx!==1||rx_data_valid||framing_error||parity_error||overrun)$fatal(1,"UART reset flush");
        reset=0;rx_data_ready=1;#(2*PERIOD);
        fork send_tx('h35);check_tx('h35);join
        queue_rx('hca);drive_rx('hca,0,-1);#(3*PERIOD);
        if(received!=queued||rx_data_valid)$fatal(1,"UART final output count");
        $display("PASS UART framing DIV=%0d %0d/%0d/%0d: %0d exhaustive TX/RX values, exact stop duration, back-to-back RX, parity/stop errors, false start, break, stalls, reset",DIV,BITS,MODE,STOPS,COUNT);$finish;
    end
    initial begin #5000000;$fatal(1,"UART framing timeout");end
endmodule
