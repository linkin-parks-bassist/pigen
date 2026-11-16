`timescale 1ns/1ps
module i2c_target_tb;
    parameter integer ADDR=82,HALF=107,SETUP=4;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg master_scl_low=0,master_sda_low=0;
    wire scl_low,sda_low,active,reading,completed;
    wire scl_i=!(master_scl_low||scl_low),sda_i=!(master_sda_low||sda_low);
    reg [7:0] tx=0;
    reg tx_valid=0,rx_ready=0;
    wire tx_ready,rx_valid;
    wire [7:0] rx;
    pigen_i2c_target dut(.*);
    reg [7:0] expected[0:1023];
    integer queued=0,received=0,checks=0,cycle=0,trace,checkfile,completions=0,stretches=0;
    reg [7:0] got;
    initial begin
        trace=$fopen("trace.csv","w");$fwrite(trace,"cycle,scl_i,sda_i,scl_low,sda_low,active,reading,tx_valid,tx_ready,rx_valid,rx_ready,reset\n");
        checkfile=$fopen("checks.csv","w");$fwrite(checkfile,"direction,index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,i2c_target_tb);
    end
    always @(posedge clk) begin
        cycle=cycle+1;
        if(cycle<1800)$fwrite(trace,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,scl_i,sda_i,scl_low,sda_low,active,reading,tx_valid,tx_ready,rx_valid,rx_ready,reset);
        if(!reset)begin
            if(completed)completions=completions+1;
            if(!master_scl_low && scl_low)stretches=stretches+1;
            if(rx_valid&&rx_ready)begin
                if(received>=queued || rx!==expected[received])$fatal(1,"I2C target RX %0d got=%h expected=%h",received,rx,expected[received]);
                $fwrite(checkfile,"rx,%0d,%0d,%0d\n",received,expected[received],rx);received=received+1;checks=checks+1;
            end
        end
    end
    always @(sda_low) if(!reset && scl_i)$fatal(1,"target changed SDA drive while SCL high");
    // Keep asynchronous stimulus off the simulator's active sampling edge.
    task avoid_edge;
        begin if($realtime==10.0*$floor($realtime/10.0)+5.0) #0.001;end
    endtask
    task load_tx(input [7:0] value);
        begin @(negedge clk);tx=value;tx_valid=1;do @(posedge clk);while(!tx_ready);@(negedge clk);tx_valid=0;end
    endtask
    task start_bus;
        begin
            avoid_edge();master_scl_low=1;avoid_edge();master_sda_low=0;#HALF;
            avoid_edge();master_scl_low=0;wait(scl_i===1);#HALF;
            avoid_edge();master_sda_low=1;#HALF;avoid_edge();master_scl_low=1;#HALF;
        end
    endtask
    task stop_bus;
        begin
            avoid_edge();master_scl_low=1;avoid_edge();master_sda_low=1;#HALF;
            avoid_edge();master_scl_low=0;wait(scl_i===1);#HALF;
            avoid_edge();master_sda_low=0;#HALF;
            if(active||scl_low||sda_low)$fatal(1,"STOP did not release target");
        end
    endtask
    task bit_clock(input bit value,output bit sample);
        begin
            avoid_edge();master_sda_low=!value;#(HALF+3);
            avoid_edge();master_scl_low=0;wait(scl_i===1);#(HALF+7);sample=sda_i;
            avoid_edge();master_scl_low=1;
        end
    endtask
    task write_byte(input [7:0] value,input bit expect_ack);
        bit sample;
        begin
            for(integer b=7;b>=0;b=b-1)bit_clock(value[b],sample);
            bit_clock(1,sample);
            if(sample!==!expect_ack)$fatal(1,"I2C target ACK for %h got=%b expected=%b",value,sample,!expect_ack);
            #HALF;
        end
    endtask
    task read_byte(input [7:0] value,input bit nack);
        bit sample;reg [7:0] observed;
        begin
            observed=0;
            for(integer b=7;b>=0;b=b-1)begin bit_clock(1,sample);observed[b]=sample;end
            bit_clock(nack,sample);avoid_edge();master_sda_low=0;#HALF;
            if(observed!==value)$fatal(1,"I2C target TX got=%h expected=%h",observed,value);
            $fwrite(checkfile,"tx,%0d,%0d,%0d\n",checks,value,observed);checks=checks+1;
        end
    endtask
    task queue_rx(input [7:0] value);
        begin expected[queued]=value;queued=queued+1;end
    endtask
    initial begin : stimulus
        integer old_received,old_stretches;bit sample;
        repeat(6)@(negedge clk);reset=0;repeat(6)@(negedge clk);rx_ready=1;
        // Every seven-bit address, including reserved addresses, both directions.
        for(integer a=0;a<128;a=a+1)begin
            start_bus();write_byte(a*2,a==ADDR);stop_bus();
            if(a!=ADDR)begin start_bus();write_byte(a*2+1,0);write_byte(8'hff,0);stop_bus();end
        end
        if(received || rx_valid)$fatal(1,"address bytes leaked into RX");
        start_bus();write_byte(ADDR*2,1);
        for(integer n=0;n<256;n=n+1)begin queue_rx(n[7:0]);write_byte(n[7:0],1);end
        stop_bus();if(received!=256)$fatal(1,"missing exhaustive writes");
        load_tx(8'ha5);start_bus();write_byte(ADDR*2+1,1);
        for(integer n=0;n<256;n=n+1)begin
            if(n!=0)load_tx((n*73)^8'ha5);
            read_byte((n*73)^8'ha5,n==255);
        end
        stop_bus();
        // Full RX stretches the ACK until the old byte is consumed.
        @(negedge clk);rx_ready=0;start_bus();write_byte(ADDR*2,1);queue_rx(8'hc7);write_byte(8'hc7,1);
        queue_rx(8'h7d);old_stretches=stretches;
        fork
            write_byte(8'h7d,1);
            begin
                wait(scl_low);repeat(30)@(negedge clk);
                if(!scl_low || !rx_valid || rx!==8'hc7)$fatal(1,"RX stretch/retention failed");
                rx_ready=1;
            end
        join
        stop_bus();if(stretches<=old_stretches)$fatal(1,"no RX stretch observed");
        // Empty TX stretches until the application supplies a byte.
        old_stretches=stretches;
        fork
            begin start_bus();write_byte(ADDR*2+1,1);read_byte(8'h66,1);stop_bus();end
            begin wait(active&&reading&&scl_low);repeat(30)@(negedge clk);if(!scl_low)$fatal(1,"TX stretch released early");load_tx(8'h66);end
        join
        if(stretches<=old_stretches)$fatal(1,"no TX stretch observed");
        // Combined write/repeated START/read/NACK/repeated START/write.
        start_bus();write_byte(ADDR*2,1);queue_rx(8'h06);write_byte(8'h06,1);
        load_tx(8'hda);start_bus();write_byte(ADDR*2+1,1);read_byte(8'hda,1);
        start_bus();write_byte(ADDR*2,1);queue_rx(8'ha2);write_byte(8'ha2,1);stop_bus();
        // STOP or a repeated START aborts every incomplete write position.
        for(integer bits=1;bits<8;bits=bits+1)begin
            old_received=received;start_bus();write_byte(ADDR*2,1);
            repeat(bits)bit_clock(1,sample);
            stop_bus();if(received!=old_received || rx_valid)$fatal(1,"partial write leaked");
            start_bus();write_byte(ADDR*2,1);repeat(bits)bit_clock(0,sample);
            start_bus();write_byte((ADDR^1)*2,0);stop_bus();
            if(received!=old_received || rx_valid)$fatal(1,"repeated START did not abort partial write");
        end
        // Partial address reception also restarts cleanly on STOP/repeated START.
        for(integer bits=1;bits<8;bits=bits+1)begin
            start_bus();repeat(bits)bit_clock(1,sample);stop_bus();
            start_bus();repeat(bits)bit_clock(0,sample);
            start_bus();write_byte((ADDR^1)*2,0);stop_bus();
        end
        // A NACK or partial read must not consume the queued following byte.
        for(integer bits=1;bits<=8;bits=bits+1)begin
            load_tx(8'hff);start_bus();write_byte(ADDR*2+1,1);
            if(bits==8)begin
                fork
                    read_byte(8'hff,1);
                    begin #(HALF*5);load_tx(8'h36);end
                join
            end else begin
                repeat(bits)bit_clock(1,sample);
                load_tx(8'h36);
            end
            stop_bus();start_bus();write_byte(ADDR*2+1,1);read_byte(8'h36,1);stop_bus();
        end
        // Reset releases a target stalled waiting for TX, then resynchronizes.
        start_bus();write_byte(ADDR*2+1,1);wait(scl_low);reset=1;
        repeat(5)@(negedge clk);if(scl_low||sda_low||active||rx_valid)$fatal(1,"reset did not flush/release");
        avoid_edge();master_scl_low=0;avoid_edge();master_sda_low=0;reset=0;repeat(8)@(negedge clk);
        start_bus();write_byte(ADDR*2,1);queue_rx(8'h96);write_byte(8'h96,1);stop_bus();
        if(received!=queued || rx_valid)$fatal(1,"missing RX %0d/%0d",received,queued);
        $display("PASS I2C target ADDRESS=%0h HALF=%0dns SETUP=%0d: exhaustive addresses and bytes, ACK/NACK, RX/TX stretch, repeated START, partial aborts, reset; RX=%0d checks=%0d",ADDR,HALF,SETUP,received,checks);$finish;
    end
    initial begin #6000000;$fatal(1,"I2C target timeout");end
endmodule
