module transfer_flow_tb;
    reg clk=0,reset=1,step_right=1;
    always #5 clk=~clk;
    reg [7:0] ingress=2;
    reg ingress_valid=0,egress_ready=1;
    wire ingress_ready,egress_valid;
    wire [9:0] egress;
    pigen_transfer_flow dut(.*);
    integer cycle=0,sent=0,received=0,trace_file,checks_file;
    reg [7:0] inputs[0:5];
    reg [9:0] expected[0:5],held;
    reg stalled=0,accepted=0;
    initial begin inputs[0]=2;inputs[1]=5;inputs[2]=9;inputs[3]=3;inputs[4]=7;inputs[5]=1;end
    always @(posedge clk)begin
        cycle=cycle+1;
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",
            cycle,reset,ingress_valid,ingress_ready,ingress,step_right,egress_valid,egress_ready,egress,
            dut.left__pigen_buf.out_valid,dut.left__pigen_buf.in_ready,dut.left__pigen_buf.packet_out,
            dut.right__pigen_buf.out_valid,dut.right__pigen_buf.in_ready,dut.right__pigen_buf.packet_out,
            dut.doubled__pigen_buf.out_valid,dut.doubled__pigen_buf.in_ready,dut.doubled__pigen_buf.packet_out,
            dut.incremented__pigen_buf.out_valid,dut.incremented__pigen_buf.in_ready,dut.incremented__pigen_buf.packet_out,
            dut.egress__pigen_buf.in_ready,
            ingress_valid && ingress_ready,
            dut.left__pigen_buf.out_valid && dut.left__pigen_buf.out_ready,
            dut.right__pigen_buf.out_valid && dut.right__pigen_buf.out_ready,
            dut.doubled__pigen_buf.out_valid && dut.doubled__pigen_buf.out_ready,
            egress_valid && egress_ready,
            dut.ingress,
            dut.ingress_valid,
            dut.ingress_ready,
            dut.step_right,
            dut.egress,
            dut.egress_valid,
            dut.egress_ready,
            dut.left,
            dut.left__pigen_valid,
            dut.left__pigen_out_ready,
            dut.left__pigen_in_valid,
            dut.left__pigen_in_ready,
            dut.left__pigen_packet_in,
            dut.right,
            dut.right__pigen_valid,
            dut.right__pigen_out_ready,
            dut.right__pigen_in_valid,
            dut.right__pigen_in_ready,
            dut.right__pigen_packet_in,
            dut.doubled,
            dut.doubled__pigen_valid,
            dut.doubled__pigen_out_ready,
            dut.doubled__pigen_in_valid,
            dut.doubled__pigen_in_ready,
            dut.doubled__pigen_packet_in,
            dut.incremented,
            dut.incremented__pigen_valid,
            dut.incremented__pigen_out_ready,
            dut.incremented__pigen_in_valid,
            dut.incremented__pigen_in_ready,
            dut.incremented__pigen_packet_in,
            dut.egress__pigen_in_valid,
            dut.egress__pigen_in_ready,
            dut.egress__pigen_packet_in);
        if(!reset)begin
            if(stalled && (!egress_valid || egress!==held))$fatal(1,"flow stalled result changed");
            accepted=ingress_valid && ingress_ready;
            if(accepted)begin expected[sent]=10'(3*ingress+1);sent=sent+1;end
            if(egress_valid && egress_ready)begin
                if(received>=sent || egress!==expected[received])$fatal(1,"flow wrong result");
                $fwrite(checks_file,"%0d,%0d,%0d\n",received,expected[received],egress);received=received+1;
            end
            stalled=egress_valid && !egress_ready;held=egress;
        end
    end
    initial begin
        trace_file=$fopen("slots.csv","w");
        $fwrite(trace_file,"cycle,reset,in_valid,in_ready,in_data,right_enable,out_valid,out_ready,out_data,a_valid,a_ready,a_data,b_valid,b_ready,b_data,d_valid,d_ready,d_data,i_valid,i_ready,i_data,sum_ready,fork_fire,left_fire,right_fire,join_fire,drain_fire,ingress,ingress_valid,ingress_ready,step_right,egress,egress_valid,egress_ready,left,left__pigen_valid,left__pigen_out_ready,left__pigen_in_valid,left__pigen_in_ready,left__pigen_packet_in,right,right__pigen_valid,right__pigen_out_ready,right__pigen_in_valid,right__pigen_in_ready,right__pigen_packet_in,doubled,doubled__pigen_valid,doubled__pigen_out_ready,doubled__pigen_in_valid,doubled__pigen_in_ready,doubled__pigen_packet_in,incremented,incremented__pigen_valid,incremented__pigen_out_ready,incremented__pigen_in_valid,incremented__pigen_in_ready,incremented__pigen_packet_in,egress__pigen_in_valid,egress__pigen_in_ready,egress__pigen_packet_in\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,transfer_flow_tb);
        repeat(3)@(negedge clk);reset=0;ingress_valid=1;
        while(cycle<65)begin
            @(negedge clk);
            if(accepted)begin ingress_valid=sent<6;if(sent<6)ingress=inputs[sent];end
            step_right=cycle%3!=0;
            egress_ready=!(cycle>=10 && cycle<24);
        end
        if(sent!=6 || received!=6)$fatal(1,"flow counts %0d %0d",sent,received);
        $display("PASS explicit buf fork/join: 6 ordered 3N+1 results, asymmetric work, backpressure");$finish;
    end
    initial begin #2000;$fatal(1,"flow timeout");end
endmodule
