module i2c_tb;
    parameter HALF=4;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [11:0] command=0;
    reg command_valid=0,response_ready=0;
    wire command_ready,response_valid,scl_low,sda_low;
    wire [8:0] response;
    reg slave_data_low=0,slave_clock_low=0;
    wire scl_i=!(scl_low || slave_clock_low);
    wire sda_i=!(sda_low || slave_data_low);
    pigen_i2c_byte_controller dut(.*);
    integer starts=0,stops=0,checks=0,trace_cycle=0,trace_file,checks_file;
    always @(negedge sda_i) if(!reset && scl_i) starts=starts+1;
    always @(posedge sda_i) if(!reset && scl_i) stops=stops+1;
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,scl,sda,scl_low,sda_low,stretch,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,i2c_tb);
    end
    always @(negedge clk) begin
        trace_cycle=trace_cycle+1;
        if(trace_cycle<1800) $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",trace_cycle,scl_i,sda_i,scl_low,sda_low,slave_clock_low,reset);
    end
    task submit(input reg[11:0] value);
        @(negedge clk);command=value;command_valid=1;
        do @(posedge clk);while(!command_ready);
        @(negedge clk);command_valid=0;
    endtask
    // Independent wire-level target: count exactly eight data clocks plus ACK.
    task target(input bit reading,input reg[7:0] value,input bit nack_reply,input bit expected_nack,input bit stretch);
        reg[7:0] got;
        @(negedge sda_i); // START while SCL high
        if(!scl_i) $fatal(1,"I2C missing START");
        @(negedge scl_i);
        for(integer b=7;b>=0;b=b-1) begin
            if(reading) slave_data_low=!value[b];
            if(stretch && b==4) begin
                slave_clock_low=1;repeat(13) @(negedge clk);
                if(scl_i) $fatal(1,"I2C clock stretch ignored");
                slave_clock_low=0;
            end
            @(posedge scl_i);got[b]=sda_i;@(negedge scl_i);
        end
        if(reading) slave_data_low=0;
        else slave_data_low=!nack_reply;
        @(posedge scl_i);
        if(reading && sda_i!==expected_nack) $fatal(1,"I2C read ACK/NACK");
        @(negedge scl_i);slave_data_low=0;
        if(!reading && got!==value) $fatal(1,"I2C write %h expected %h",got,value);
    endtask
    task transaction(input bit reading,input reg[7:0] value,input bit nack_reply,input bit nack_read,input bit stretch,input bit send_stop);
        reg[8:0] expected;
        fork
            submit({1'b1,send_stop,reading,nack_read,value});
            target(reading,value,nack_reply,nack_read,stretch);
        join
        wait(response_valid);@(negedge clk);
        expected={(!reading && nack_reply),value};
        repeat(7) begin
            if(!response_valid || response!==expected) $fatal(1,"I2C stalled response %h expected %h",response,expected);
            @(negedge clk);
        end
        $fwrite(checks_file,"%0d,%0d,%0d\n",checks,expected,response);checks=checks+1;
        response_ready=1;@(negedge clk);response_ready=0;
        if(send_stop && (!scl_i || !sda_i)) $fatal(1,"I2C STOP did not release bus");
    endtask
    initial begin
        repeat(4) @(negedge clk);reset=0;repeat(3) @(negedge clk);
        for(integer k=0;k<32;k=k+1) begin
            transaction(0,(k*73)^8'ha5,k%3==0,0,k%2==0,1);
            transaction(1,(k*29)^8'h5a,0,k%2==0,k%3==0,1);
        end
        transaction(0,8'ha0,0,0,1,0);
        if(scl_i) $fatal(1,"I2C bus must stay held without STOP");
        transaction(1,8'h67,0,1,1,1);
        if(starts!=66 || stops!=65) $fatal(1,"I2C starts/stops %0d/%0d",starts,stops);
        fork submit(12'hc55); begin wait(!sda_i);repeat(5) @(negedge clk);reset=1;end join
        repeat(3) @(negedge clk);
        if(scl_low || sda_low || response_valid) $fatal(1,"I2C reset releases bus");
        $display("PASS i2c HALF=%0d 66 read/write bytes, repeated START, ACK/NACK, stretch, stalls, START/STOP, reset",HALF);$finish;
    end
    initial begin #2000000;$fatal(1,"I2C timeout");end
endmodule
