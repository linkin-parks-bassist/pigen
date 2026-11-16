`timescale 1ns/1ps
// Independent pin-level BL=1 x16 SDR model. Protocol state follows commands, not
// DUT state names. Memory is full 24-bit word-addressed, so high address aliasing
// is observable. Reset marks contents unqualified and requires reinitialization;
// bank state and in-flight pin reads are retained to exercise warm restart.
module sdram_model #(
    parameter integer INIT=20000, CL=3, RP=2, RCD=2, RAS=5, WR=2, RFC=7, MRD=2, INTERVAL=700
)(
    input wire clk,reset_epoch,
    input wire [3:0] command,
    input wire [12:0] address,
    input wire [1:0] bank,dqm,
    inout wire [15:0] dq,
    output reg drive=0,
    output integer refreshes=0,reads=0,writes=0,activations=0
);
    reg [15:0] memory[0:16777215];
    reg [15:0] data_out=0,read_value;
    assign dq=drive?data_out:16'bz;
    reg [3:0] open_bank=0;
    reg [12:0] row[0:3];
    integer act[0:3],pre[0:3],wr[0:3],rd[0:3];
    integer cycle=0,epoch_start=0,phase=0,available=0,last_refresh=0,last_act=-100000;
    integer due=-1,latency=0,i,index;
    reg [1:0] mask_one=0,mask_two=0;
    initial for(integer n=0;n<4;n=n+1)begin act[n]=-100000;pre[n]=-100000;wr[n]=-100000;rd[n]=-100000;end
    always @(posedge clk) begin
        cycle=cycle+1;
        drive <= #2 0;
        if(cycle==due) begin
            if(mask_two!=0)$fatal(1,"model read masked at output");
            data_out<=#2 read_value;drive<=#2 1;
        end
        mask_two=mask_one;mask_one=dqm;
        if(phase==4 && !reset_epoch && cycle-last_refresh>INTERVAL)
            $fatal(1,"model refresh deadline exceeded: gap %0d",cycle-last_refresh);
        if(cycle>1 && command[3]===1'b0 && command[2:0]!=3'b111)begin
            if(!reset_epoch && cycle<available)$fatal(1,"model command before RFC/MRD recovery at %0d",cycle);
            case(command[2:0])
                3'b010:begin // PRECHARGE
                    if(phase==0)begin
                        if(cycle-epoch_start<INIT || !address[10])$fatal(1,"model initialization delay/PALL");
                        phase=1;
                    end else if(phase!=4)$fatal(1,"model unexpected initialization PRE");
                    for(i=0;i<4;i=i+1)if(address[10] || bank==i)begin
                        if(open_bank[i] && (cycle-act[i]<RAS || cycle-wr[i]<WR || cycle-rd[i]<latency))
                            $fatal(1,"model PRE violated RAS/WR/read recovery");
                        open_bank[i]=0;pre[i]=cycle;
                    end
                end
                3'b001:begin // AUTO REFRESH
                    if(open_bank!=0)$fatal(1,"model REF with open bank");
                    for(i=0;i<4;i=i+1)if(cycle-pre[i]<RP)$fatal(1,"model REF before RP");
                    if(phase==1 || phase==2)phase=phase+1;
                    else if(phase!=4)$fatal(1,"model REF initialization sequence");
                    refreshes=refreshes+1;last_refresh=cycle;available=cycle+RFC;
                end
                3'b000:begin // LOAD MODE
                    if(phase!=3 || open_bank!=0 || bank!=0 || address!==(13'h200|(CL<<4)))
                        $fatal(1,"model mode register/initial refresh sequence %h",address);
                    for(i=0;i<4;i=i+1)if(cycle-pre[i]<RP)$fatal(1,"model MODE before RP");
                    latency=address[6:4];phase=4;available=cycle+MRD;
                end
                3'b011:begin // ACTIVE
                    if(phase!=4 || open_bank[bank] || cycle-pre[bank]<RP || cycle-act[bank]<RAS+RP || cycle-last_act<2)
                        $fatal(1,"model ACT bank/recovery violation at %0d",cycle);
                    row[bank]=address;open_bank[bank]=1;act[bank]=cycle;last_act=cycle;activations=activations+1;
                end
                3'b100,3'b101:begin // WRITE or READ
                    if(phase!=4 || !open_bank[bank] || cycle-act[bank]<RCD || address[12:9]!=0)
                        $fatal(1,"model access row/RCD/column/AP violation at %0d",cycle);
                    index={bank,row[bank],address[8:0]};
                    if(command[0])begin
                        if(dqm!=0)$fatal(1,"model read DQM");
                        read_value=memory[index];due=cycle+latency-1;rd[bank]=cycle;reads=reads+1;
                    end else begin
                        if(!dqm[0])memory[index][7:0]=dq[7:0];
                        if(!dqm[1])memory[index][15:8]=dq[15:8];
                        if((!dqm[0] && $isunknown(dq[7:0])) || (!dqm[1] && $isunknown(dq[15:8])))
                            $fatal(1,"model invalid write bus");
                        wr[bank]=cycle;writes=writes+1;
                    end
                end
                default:$fatal(1,"model unsupported command %h",command);
            endcase
        end
        if(reset_epoch)begin phase=0;epoch_start=cycle;end
        if(phase==4 && !reset_epoch)for(i=0;i<4;i=i+1)
            if(open_bank[i] && cycle-act[i]>12000)$fatal(1,"model RAS maximum exceeded");
    end
endmodule
