`timescale 1ns/1ps
module sdram_tb;
    parameter integer INIT=20000,CL=3,RP=2,RCD=2,RAS=5,WR=2,RFC=7,MRD=2,INTERVAL=700;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [42:0] request=0;
    reg request_valid=0,response_ready=0;
    wire request_ready,response_valid,initialized,dq_oe;
    wire [16:0] response;
    wire [3:0] command;
    wire [12:0] address;
    wire [1:0] bank,dqm;
    wire [15:0] dq_out,dq_in;
    tri [15:0] dq;
    pigen_sdram_controller dut(.*);
    pigen_sdram_dq_pins pins(.*);
    wire model_drive;
    wire [31:0] refreshes,reads,writes,activations;
    sdram_model #(.INIT(INIT),.CL(CL),.RP(RP),.RCD(RCD),.RAS(RAS),.WR(WR),.RFC(RFC),.MRD(MRD),.INTERVAL(INTERVAL))
        memory(.clk(clk),.reset_epoch(reset),.command(command),.address(address),.bank(bank),.dqm(dqm),.dq(dq),
               .drive(model_drive),.refreshes(refreshes),.reads(reads),.writes(writes),.activations(activations));
    reg [23:0] locations[0:95];
    reg [15:0] expected[0:95];
    reg [16:0] responses[0:8191],held;
    integer issued=0,received=0,checks=0,cycle=0,stalls=0,epochs=0;
    integer trace_file,checks_file,commands_file,i,n,k,old_refresh,old_issued;
    reg stalled=0;
    reg [31:0] rng=32'hf1ea4c97;
    reg random_ready=0;
    function integer find_location(input [23:0] a);
        integer j;
        begin find_location=-1;for(j=0;j<96;j=j+1)if(locations[j]==a)find_location=j;end
    endfunction
    always @(posedge clk)begin
        cycle=cycle+1;
        if(reset)begin issued=0;received=0;stalled=0;end
        else begin
            if(stalled && (!response_valid || response!==held))$fatal(1,"SDRAM stalled response changed");
            if(request_valid && request_ready)begin
                if(!initialized)$fatal(1,"SDRAM accepted before initialization");
                n=find_location(request[39:16]);if(n<0)$fatal(1,"scoreboard unknown address");
                if(request[42])begin
                    if(request[40])expected[n][7:0]=request[7:0];
                    if(request[41])expected[n][15:8]=request[15:8];
                    responses[issued]={1'b1,16'b0};
                end else responses[issued]={1'b0,expected[n]};
                issued=issued+1;
            end
            if(response_valid && response_ready)begin
                if(received>=issued || response!==responses[received])
                    $fatal(1,"SDRAM response %0d got %h expected %h at cycle %0d",received,response,responses[received],cycle);
                $fwrite(checks_file,"%0d,%0d,%0d\n",checks,responses[received],response);checks=checks+1;received=received+1;
            end
            stalled=response_valid && !response_ready;held=response;if(stalled)stalls=stalls+1;
        end
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",
            cycle,reset,initialized,request_valid,request_ready,response_valid,response_ready,
            command[2],command[1],command[0],dq_oe,model_drive,bank,address,dqm,refreshes);
        if(command!=7 && command[3]==0)$fwrite(commands_file,"%0d,%0d,%0d,%0d,%0d\n",cycle,command,bank,address,dqm);
        #3;if(dq_oe && model_drive)$fatal(1,"SDRAM DQ contention");
    end
    always @(negedge clk)if(random_ready)begin
        rng=rng*1664525+1013904223;response_ready=rng[3:0]>5;
    end
    task send(input bit wr_en,input [23:0] a,input [15:0] data,input [1:0] strobes);
        @(negedge clk);request={wr_en,strobes,a,data};request_valid=1;
        do @(posedge clk);while(!request_ready);
        @(negedge clk);request_valid=0;
    endtask
    task drain;
        @(negedge clk);random_ready=0;response_ready=1;
        wait(received==issued);repeat(4)@(negedge clk);
    endtask
    task restart;
        @(negedge clk);reset=1;request_valid=0;random_ready=0;response_ready=0;
        repeat(3)@(negedge clk);reset=0;epochs=epochs+1;
        wait(initialized);repeat(3)@(negedge clk);
        if(response_valid)$fatal(1,"stale response after SDRAM reset");
    endtask
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,reset,initialized,in_valid,in_ready,out_valid,out_ready,ras_n,cas_n,we_n,dq_write,dq_read,bank,address,dqm,refreshes\n");
        commands_file=$fopen("commands.csv","w");$fwrite(commands_file,"cycle,command,bank,address,dqm\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,sdram_tb);
        // Test every bank, row/column boundaries and each of the 24 address bits.
        for(i=0;i<96;i=i+1)begin
            locations[i]=24'(i);
            if(i<24)locations[i]=24'b1<<i;
            else if(i<48)locations[i]=24'hffffff^(24'b1<<(i-24));
            else if(i<64)locations[i]={2'(i%4),13'(i*7),9'(i%2?511:0)};
            else locations[i]=24'h824000+24'(i*137);
            expected[i]=0;
        end
        repeat(4)@(negedge clk);reset=0;
        // Pending input is offered during power-up and must not fire early.
        send(1,locations[0],16'ha55a,3);response_ready=1;drain();
        random_ready=1;
        for(i=0;i<96;i=i+1)send(1,locations[i],16'(i*7919+16'h1234),3);
        for(i=95;i>=0;i=i-1)send(0,locations[i],0,0);
        // All four byte masks, with read-after-write checks and alternating banks.
        for(k=0;k<4;k=k+1)for(i=0;i<96;i=i+1)begin
            send(1,locations[i],16'(i*19+k*1523),2'(k));send(0,locations[i],0,0);
        end
        drain();
        // Fill response storage and hold it over several refresh deadlines.
        response_ready=0;send(0,locations[1],0,0);send(0,locations[2],0,0);
        wait(response_valid);old_refresh=refreshes;old_issued=issued;
        @(negedge clk);request={1'b0,2'b00,locations[7],16'b0};request_valid=1;
        repeat(INTERVAL*4)@(negedge clk);
        if(refreshes-old_refresh<3 || request_ready || issued!=old_issued)$fatal(1,"refresh/backpressure failed");
        response_ready=1;
        do @(posedge clk);while(!request_ready);
        @(negedge clk);request_valid=0;drain();
        // Reset with stalled responses, then while an SDR row is open.
        response_ready=0;send(0,locations[3],0,0);wait(response_valid);restart();
        response_ready=1;send(1,locations[4],16'hb00b,3);wait(memory.open_bank!=0);restart();
        response_ready=1;send(1,locations[5],16'hcafe,3);drain();send(0,locations[5],0,0);drain();
        // Reset across a pin READ; the old memory burst may still appear on DQ.
        send(0,locations[5],0,0);wait(command==4'b0101);restart();
        response_ready=1;send(1,locations[6],16'h5a3c,3);drain();send(0,locations[6],0,0);drain();
        if(checks<960 || reads<480 || writes<480 || refreshes<20 || stalls<INTERVAL*3 || epochs!=3)
            $fatal(1,"SDRAM coverage insufficient checks=%0d read=%0d write=%0d refresh=%0d",checks,reads,writes,refreshes);
        $display("PASS sdram CAS=%0d timing RP/RCD/RAS=%0d/%0d/%0d checks=%0d read=%0d write=%0d refresh=%0d stalls=%0d reset epochs=%0d",CL,RP,RCD,RAS,checks,reads,writes,refreshes,stalls,epochs);$finish;
    end
    initial begin #10000000;$fatal(1,"SDRAM timeout cycle=%0d issued=%0d received=%0d",cycle,issued,received);end
endmodule
