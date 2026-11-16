module axis_tb;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [31:0] s_axis_tdata=0;
    reg [3:0] s_axis_tkeep=0;
    reg s_axis_tlast=0,s_axis_tvalid=0,m_axis_tready=0;
    wire s_axis_tready,m_axis_tvalid,m_axis_tlast;
    wire [31:0] m_axis_tdata;
    wire [3:0] m_axis_tkeep;
    pigen_axis_fifo dut(.*);
    reg [36:0] expected[0:4095],held;
    integer sent=0,received=0,cycle=0,stalls=0,simultaneous=0;
    reg stalled=0,accepted=0;
    integer trace_file,checks_file,slots_file,slot_cycle=0;
    initial begin
        slots_file=$fopen("slots.csv","w");
        $fwrite(slots_file,"cycle,reset,in_valid,in_ready,in_data,out_valid,out_ready,out_data,count,read_ptr,write_ptr,slot0,slot1,slot2,slot3,push,pop\n");
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,in_valid,in_ready,out_valid,out_ready,sent,received,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,axis_tb);
    end
    always @(negedge clk) $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,s_axis_tvalid,s_axis_tready,m_axis_tvalid,m_axis_tready,sent,received,reset);
    // Capture immediately before the active edge: occupancy/payloads describe
    // the old state and push/pop describe exactly the transition at this edge.
    always @(posedge clk) begin
        slot_cycle=slot_cycle+1;
        $fwrite(slots_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",
            slot_cycle,reset,s_axis_tvalid,s_axis_tready,{s_axis_tlast,s_axis_tkeep,s_axis_tdata},
            m_axis_tvalid,m_axis_tready,{m_axis_tlast,m_axis_tkeep,m_axis_tdata},
            dut.s_axis__pigen_channel_beat__pigen_fifo.count,dut.s_axis__pigen_channel_beat__pigen_fifo.read_pointer,
            dut.s_axis__pigen_channel_beat__pigen_fifo.write_pointer,
            dut.s_axis__pigen_channel_beat__pigen_fifo.packets[0],dut.s_axis__pigen_channel_beat__pigen_fifo.packets[1],
            dut.s_axis__pigen_channel_beat__pigen_fifo.packets[2],dut.s_axis__pigen_channel_beat__pigen_fifo.packets[3],
            dut.s_axis__pigen_channel_beat__pigen_fifo.push,dut.s_axis__pigen_channel_beat__pigen_fifo.pop);
    end
    reg [31:0] rng=32'hbade1234;
    always @(posedge clk) begin
        if(reset) begin sent=0;received=0;stalled=0;end
        else begin
            if(stalled && (!m_axis_tvalid || {m_axis_tlast,m_axis_tkeep,m_axis_tdata}!==held))
                $fatal(1,"AXIS changed payload/valid while stalled");
            accepted=s_axis_tvalid && s_axis_tready;
            if(accepted) begin
                expected[sent]={s_axis_tlast,s_axis_tkeep,s_axis_tdata};sent=sent+1;
            end
            if(m_axis_tvalid && m_axis_tready) begin
                if(received>=sent || {m_axis_tlast,m_axis_tkeep,m_axis_tdata}!==expected[received])
                    $fatal(1,"AXIS reordered/corrupted beat %0d",received);
                $fwrite(checks_file,"%0d,%0d,%0d\n",received,expected[received],{m_axis_tlast,m_axis_tkeep,m_axis_tdata});
                received=received+1;
            end
            if(s_axis_tvalid && s_axis_tready && m_axis_tvalid && m_axis_tready) simultaneous=simultaneous+1;
            stalled=m_axis_tvalid && !m_axis_tready;
            held={m_axis_tlast,m_axis_tkeep,m_axis_tdata};
            if(stalled) stalls=stalls+1;
        end
    end
    task drive(input integer cycles,input bit drain);
        repeat(cycles) begin
            @(negedge clk);cycle=cycle+1;rng=rng*1664525+1013904223;
            // A producer must preserve a pending beat until accepted.
            if(!s_axis_tvalid || accepted) begin
                s_axis_tvalid=!drain && (rng[4:0]!=0);
                s_axis_tdata=rng;s_axis_tkeep=rng[11:8];s_axis_tlast=(cycle%7==0);
            end
            m_axis_tready=drain || (cycle>100 && rng[7:5]>2);
        end
    endtask
    initial begin
        repeat(4) @(negedge clk);reset=0;
        drive(100,0);
        // With no receiver, the finite FIFO must backpressure the source.
        if(sent!=5 || s_axis_tready) $fatal(1,"AXIS capacity/backpressure %0d",sent);
        drive(1900,0);drive(30,1);
        if(sent!=received || stalls<100 || simultaneous<100) $fatal(1,"AXIS insufficient coverage sent=%0d got=%0d",sent,received);
        // Reset a nonempty/stalled queue, then prove a fresh epoch has no stale beats.
        drive(20,0);@(negedge clk);reset=1;s_axis_tvalid=0;
        repeat(3) @(negedge clk);reset=0;drive(300,0);drive(30,1);
        if(sent!=received) $fatal(1,"AXIS reset epoch lost data");
        $display("PASS axis randomized data/keep/last, %0d final-epoch beats, stalls=%0d simultaneous=%0d, capacity/reset",received,stalls,simultaneous);$finish;
    end
    initial begin #100000;$fatal(1,"AXIS timeout");end
endmodule
