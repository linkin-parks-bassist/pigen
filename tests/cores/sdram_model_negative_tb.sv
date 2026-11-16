`timescale 1ns/1ps
// Prove that the independent SDRAM checker rejects malformed pin sequences.
module sdram_model_negative_tb;
    parameter integer FAULT=0;
    reg clk=0,reset_epoch=1;
    always #5 clk=~clk;
    reg [3:0] command=7;
    reg [12:0] address=0;
    reg [1:0] bank=0,dqm=0;
    tri [15:0] dq;
    assign dq=command==4?16'habcd:16'bz;
    wire drive;
    wire [31:0] refreshes,reads,writes,activations;
    sdram_model #(.INIT(2),.CL(3),.RP(3),.RCD(4),.RAS(8),.WR(3),.RFC(5),.MRD(2),.INTERVAL(80))
        model(.*);
    task cmd(input [3:0] c,input [12:0] a);
        @(negedge clk);command=c;address=a;
        @(negedge clk);command=7;
    endtask
    initial begin
        repeat(3)@(negedge clk);reset_epoch=0;repeat(3)@(negedge clk);
        cmd(2,13'h400);repeat(3)@(negedge clk);
        cmd(1,0);repeat(5)@(negedge clk);cmd(1,0);repeat(5)@(negedge clk);
        cmd(0,FAULT==6?13'h231:13'h230);repeat(3)@(negedge clk);
        if(FAULT==5)repeat(90)@(negedge clk);
        cmd(3,0);
        if(FAULT==0)cmd(1,0);
        if(FAULT==1)begin repeat(8)@(negedge clk);cmd(2,0);cmd(3,0);end
        if(FAULT==2)cmd(5,0);
        if(FAULT==3)cmd(2,0);
        if(FAULT==4)begin repeat(8)@(negedge clk);cmd(4,0);cmd(2,0);end
        repeat(3)@(negedge clk);$display("MODEL ACCEPTED INVALID TRACE");$finish;
    end
    initial begin #10000;$fatal(1,"negative model timeout");end
endmodule
