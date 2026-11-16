module axi4_tb;
    parameter integer SEED=1;
    reg clk=0;
    reg reset=0;
    reg [3:0] s_axi_awid=0;
    reg [31:0] s_axi_awaddr=0;
    reg [7:0] s_axi_awlen=0;
    reg [2:0] s_axi_awsize=0;
    reg [1:0] s_axi_awburst=0;
    reg s_axi_awlock=0;
    reg [3:0] s_axi_awcache=0;
    reg [2:0] s_axi_awprot=0;
    reg [3:0] s_axi_awqos=0;
    reg [3:0] s_axi_awregion=0;
    reg s_axi_awvalid=0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata=0;
    reg [3:0] s_axi_wstrb=0;
    reg s_axi_wlast=0;
    reg s_axi_wvalid=0;
    wire s_axi_wready;
    wire [3:0] s_axi_bid;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready=0;
    reg [3:0] s_axi_arid=0;
    reg [31:0] s_axi_araddr=0;
    reg [7:0] s_axi_arlen=0;
    reg [2:0] s_axi_arsize=0;
    reg [1:0] s_axi_arburst=0;
    reg s_axi_arlock=0;
    reg [3:0] s_axi_arcache=0;
    reg [2:0] s_axi_arprot=0;
    reg [3:0] s_axi_arqos=0;
    reg [3:0] s_axi_arregion=0;
    reg s_axi_arvalid=0;
    wire s_axi_arready;
    wire [3:0] s_axi_rid;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rlast;
    wire s_axi_rvalid;
    reg s_axi_rready=0;
    wire [67:0] write_request;
    wire write_request_valid;
    reg write_request_ready=0;
    reg [1:0] write_reply=0;
    reg write_reply_valid=0;
    wire write_reply_ready;
    wire [31:0] read_request;
    wire read_request_valid;
    reg read_request_ready=0;
    reg [33:0] read_reply=0;
    reg read_reply_valid=0;
    wire read_reply_ready;
    always #5 clk=~clk;
    pigen_axi4_bridge dut(.*);
    reg [67:0] expected_w[0:32767];
    reg [31:0] expected_a[0:32767];
    reg [38:0] expected_r[0:32767];
    reg [5:0] expected_b[0:4095];
    integer plan_w=0,plan_a=0,plan_r=0,plan_b=0,got_w=0,got_a=0,got_r=0,got_b=0;
    integer checks=0,cycle=0,b_stalls=0,r_stalls=0,wt=-1,rt=-1;
    reg [1:0] wc;
    reg [33:0] rv;
    reg bs=0,rs=0,ws=0,as=0;
    reg [67:0] wh;
    reg [31:0] ah;
    reg [5:0] bh;
    reg [38:0] rh;
    reg random_ready=1;
    reg [31:0] rng=SEED;
    integer trace_file,checks_file,backend_file;
    function [31:0] pattern(input [31:0] a);
        pattern=(a*32'h45d9f3b)^32'hcb12e789;
    endfunction
    function [1:0] error_code(input [31:0] a);
        error_code=a[31]?3:(a[9:8]==3?2:0);
    endfunction
    function [31:0] beat_address(input [31:0] base,input integer bytes,count,burst,index);
        reg [63:0] start,region;
        begin
            start=base;region=(start/(bytes*count))*(bytes*count);
            if(burst==0 || index==0)beat_address=base;
            else if(burst==1)beat_address=32'((start/bytes)*bytes+index*bytes);
            else beat_address=32'(region+((start-region+index*bytes)%(bytes*count)));
        end
    endfunction
    function bit bad_burst(input [31:0] base,input integer size,count,burst);
        reg [63:0] finish;
        integer bytes;
        begin
            bytes=1<<size;finish=(64'(base)/bytes)*bytes+count*bytes-1;
            bad_burst=size>2 || burst==3 || (burst==0 && count>16) ||
                (burst==1 && (finish/4096)!=(64'(base)/4096)) ||
                (burst==2 && ((count!=2 && count!=4 && count!=8 && count!=16) || base%bytes!=0));
        end
    endfunction
    function [3:0] lanes(input [31:0] addr,input integer bytes);
        integer j;
        reg [63:0] first,limit,word;
        begin
            first=addr;limit=(first/bytes)*bytes+bytes;word=(first/4)*4;lanes=0;
            for(j=0;j<4;j=j+1)if(word+j>=first && word+j<limit)lanes[j]=1;
        end
    endfunction
    function [31:0] data_word(input integer tag,index);
        data_word=32'(tag)*32'h1020304 ^ 32'(index)*32'h5040302 ^ 32'h9e3779b9;
    endfunction
    function [3:0] strobes(input [31:0] a,input integer bytes,tag,index,input bit invalid);
        reg [3:0] mask;
        begin
            mask=lanes(a,bytes);
            strobes=mask & 4'(tag+index);
            if((tag+index)%5==0)strobes=mask;
            if(invalid && index==0)strobes=15;
        end
    endfunction
    always @(negedge clk)begin
        rng=rng*1664525+1013904223;
        write_request_ready=!reset && wt<0 && !write_reply_valid && rng[2:0]>1;
        read_request_ready=!reset && rt<0 && !read_reply_valid && rng[5:3]>2;
        if(random_ready)begin s_axi_bready=rng[8:6]>2;s_axi_rready=rng[11:9]>3;end
    end
    always @(posedge clk)begin
        cycle=cycle+1;
        if(reset)begin
            wt=-1;rt=-1;write_reply_valid<=0;read_reply_valid<=0;
            got_w=0;got_a=0;got_r=0;got_b=0;plan_w=0;plan_a=0;plan_r=0;plan_b=0;bs=0;rs=0;ws=0;as=0;
        end else begin
            if(ws && (!write_request_valid || write_request!==wh))$fatal(1,"AXI4 write backend changed while stalled");
            if(as && (!read_request_valid || read_request!==ah))$fatal(1,"AXI4 read backend changed while stalled");
            ws=write_request_valid && !write_request_ready;wh=write_request;
            as=read_request_valid && !read_request_ready;ah=read_request;
            if(write_reply_valid && write_reply_ready)write_reply_valid<=0;
            if(read_reply_valid && read_reply_ready)read_reply_valid<=0;
            if(wt==0)begin write_reply<=wc;write_reply_valid<=1;wt=-1;end else if(wt>0)wt=wt-1;
            if(rt==0)begin read_reply<=rv;read_reply_valid<=1;rt=-1;end else if(rt>0)rt=rt-1;
            if(write_request_valid && write_request_ready)begin
                if(got_w>=plan_w || write_request!==expected_w[got_w])
                    $fatal(1,"AXI4 backend write %0d got %h expected %h",got_w,write_request,expected_w[got_w]);
                wc=error_code(write_request[63:32]);wt=1+int'(rng[14:12]);got_w=got_w+1;
                $fwrite(backend_file,"%0d,1,%0d,%0d,%0d\n",cycle,write_request[63:32],write_request[31:0],write_request[67:64]);
            end
            if(read_request_valid && read_request_ready)begin
                if(got_a>=plan_a || read_request!==expected_a[got_a])
                    $fatal(1,"AXI4 backend read %0d got %h expected %h",got_a,read_request,expected_a[got_a]);
                rv={error_code(read_request),pattern(read_request)};rt=1+int'(rng[17:15]);got_a=got_a+1;
                $fwrite(backend_file,"%0d,0,%0d,0,0\n",cycle,read_request);
            end
            if(bs && (!s_axi_bvalid || {s_axi_bid,s_axi_bresp}!==bh))$fatal(1,"AXI4 B changed while stalled");
            if(rs && (!s_axi_rvalid || {s_axi_rlast,s_axi_rid,s_axi_rresp,s_axi_rdata}!==rh))$fatal(1,"AXI4 R changed while stalled");
            if(s_axi_bvalid && s_axi_bready)begin
                if(got_b>=plan_b || {s_axi_bid,s_axi_bresp}!==expected_b[got_b])$fatal(1,"AXI4 B %0d got %h expected %h",got_b,{s_axi_bid,s_axi_bresp},expected_b[got_b]);
                $fwrite(checks_file,"%0d,%0d,%0d\n",checks,expected_b[got_b],{s_axi_bid,s_axi_bresp});checks=checks+1;got_b=got_b+1;
            end
            if(s_axi_rvalid && s_axi_rready)begin
                if(got_r>=plan_r || {s_axi_rlast,s_axi_rid,s_axi_rresp,s_axi_rdata}!==expected_r[got_r])
                    $fatal(1,"AXI4 R %0d got %h expected %h",got_r,{s_axi_rlast,s_axi_rid,s_axi_rresp,s_axi_rdata},expected_r[got_r]);
                $fwrite(checks_file,"%0d,%0d,%0d\n",checks,expected_r[got_r],{s_axi_rlast,s_axi_rid,s_axi_rresp,s_axi_rdata});checks=checks+1;got_r=got_r+1;
            end
            bs=s_axi_bvalid && !s_axi_bready;bh={s_axi_bid,s_axi_bresp};
            rs=s_axi_rvalid && !s_axi_rready;rh={s_axi_rlast,s_axi_rid,s_axi_rresp,s_axi_rdata};
            if(bs)b_stalls=b_stalls+1;if(rs)r_stalls=r_stalls+1;
        end
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,s_axi_awvalid,s_axi_awready,s_axi_wvalid,s_axi_wready,s_axi_bvalid,s_axi_bready,s_axi_arvalid,s_axi_arready,s_axi_rvalid,s_axi_rready,s_axi_rlast,write_request_valid,read_request_valid,reset);
    end
    task automatic drive_aw(input [31:0] base,input integer size,count,burst,tag,delay_cycles);
        repeat(delay_cycles)@(negedge clk);
        @(negedge clk);s_axi_awaddr=base;s_axi_awid=4'(tag);s_axi_awlen=8'(count-1);s_axi_awsize=3'(size);s_axi_awburst=2'(burst);s_axi_awlock=tag%2;s_axi_awvalid=1;
        do @(posedge clk);while(!s_axi_awready);
        @(negedge clk);s_axi_awvalid=0;
    endtask
    task automatic drive_w(input [31:0] base,input integer size,count,burst,tag,delay_cycles,input integer last_fault,input bit wrong_strobe);
        reg [31:0] a;
        integer j;
        begin
            repeat(delay_cycles)@(negedge clk);
            for(j=0;j<count;j=j+1)begin
                a=beat_address(base,1<<size,count,burst,j);
                @(negedge clk);s_axi_wdata=data_word(tag,j);s_axi_wstrb=strobes(a,1<<size,tag,j,wrong_strobe);s_axi_wlast=last_fault==2?0:((j==count-1)||(last_fault==1 && j==0));s_axi_wvalid=1;
                do @(posedge clk);while(!s_axi_wready);
                @(negedge clk);s_axi_wvalid=0;
            end
        end
    endtask
    task automatic write_burst(input [31:0] base,input integer size,count,burst,tag,skew,input integer last_fault,input bit wrong_strobe);
        integer j;reg [31:0] a;reg [3:0] mask;reg [1:0] status,code;reg bad;
        begin
            bad=bad_burst(base,size,count,burst);status=bad?3:0;
            for(j=0;j<count;j=j+1)begin
                a=beat_address(base,1<<size,count,burst,j);mask=strobes(a,1<<size,tag,j,wrong_strobe);
                if(((last_fault==1 && j==0) || (last_fault==2 && j==count-1) || (mask & ~lanes(a,1<<size))!=0) && status<2)status=2;
                if(!bad && (mask & ~lanes(a,1<<size))==0 && mask!=0)begin
                    expected_w[plan_w]={mask,32'((a/4)*4),data_word(tag,j)};plan_w=plan_w+1;
                    code=error_code(a);if(code>status)status=code;
                end
            end
            expected_b[plan_b]={4'(tag),status};plan_b=plan_b+1;
            fork drive_aw(base,size,count,burst,tag,skew<0?-skew:0);
                 drive_w(base,size,count,burst,tag,skew>0?skew:0,last_fault,wrong_strobe);join
        end
    endtask
    task automatic read_burst(input [31:0] base,input integer size,count,burst,tag);
        integer j;reg [31:0] a;reg bad;reg [1:0] code;reg [31:0] data;
        begin
            bad=bad_burst(base,size,count,burst);
            for(j=0;j<count;j=j+1)begin
                a=(beat_address(base,1<<size,count,burst,j)/4)*4;code=bad?3:error_code(a);data=bad?0:pattern(a);
                expected_r[plan_r]={(j==count-1),4'(tag),code,data};plan_r=plan_r+1;
                if(!bad)begin expected_a[plan_a]=a;plan_a=plan_a+1;end
            end
            @(negedge clk);s_axi_araddr=base;s_axi_arid=4'(tag);s_axi_arlen=8'(count-1);s_axi_arsize=3'(size);s_axi_arburst=2'(burst);s_axi_arlock=tag%2;s_axi_arvalid=1;
            do @(posedge clk);while(!s_axi_arready);
            @(negedge clk);s_axi_arvalid=0;
        end
    endtask
    task drain;
        @(negedge clk);random_ready=0;s_axi_bready=1;s_axi_rready=1;
        wait(got_b==plan_b && got_r==plan_r);repeat(4)@(negedge clk);
        if(got_w!=plan_w || got_a!=plan_a)$fatal(1,"AXI4 missed backend commands");
        random_ready=1;
    endtask
    task restart;
        @(negedge clk);reset=1;s_axi_awvalid=0;s_axi_wvalid=0;s_axi_arvalid=0;
        repeat(3)@(negedge clk);reset=0;repeat(3)@(negedge clk);
    endtask
    integer size,burst,offset,count,tag=0;
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,aw_valid,aw_ready,w_valid,w_ready,b_valid,b_ready,ar_valid,ar_ready,r_valid,r_ready,r_last,write_backend,read_backend,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        backend_file=$fopen("backend.csv","w");$fwrite(backend_file,"cycle,write,address,data,strobes\n");
        $dumpfile("wave.vcd");$dumpvars(0,axi4_tb);
        reset=1;repeat(4)@(negedge clk);reset=0;
        for(size=0;size<3;size=size+1)for(burst=0;burst<2;burst=burst+1)for(offset=0;offset<4;offset=offset+1)begin
            tag=tag+1;write_burst(32'h100+32'(offset),size,16,burst,tag,(offset%3-1)*9,0,0);
            read_burst(32'h180+32'(offset),size,16,burst,tag);
        end
        for(size=0;size<3;size=size+1)for(count=2;count<=16;count=count*2)begin
            tag=tag+1;write_burst(32'h10fc & ~32'((1<<size)-1),size,count,2,tag,-7,0,0);
            read_burst(32'h20fc & ~32'((1<<size)-1),size,count,2,tag);
        end
        write_burst(32'h2000,2,256,1,1,7,0,0);read_burst(32'h4000,2,256,1,2);
        write_burst(32'h2f0,2,16,1,3,0,0,0);read_burst(32'h2f0,2,16,1,3);
        write_burst(32'h80000000,2,4,1,4,-9,0,0);read_burst(32'h80000000,2,4,1,4);
        write_burst(32'h40,2,4,1,5,9,1,0);write_burst(32'h41,0,4,1,6,-5,0,1);
        write_burst(32'h44,2,4,1,7,0,2,0);
        write_burst(32'hfff,2,1,1,8,-3,0,0);read_burst(32'hfff,2,1,1,8);
        write_burst(32'hfffffffc,2,4,2,9,0,0,0);read_burst(32'hfffffffc,2,4,2,9);
        write_burst(32'hfffffffc,2,4,1,9,0,0,0);read_burst(32'hfffffffc,2,4,1,9);
        // Invalid burst descriptors drain deterministically without backend traffic.
        write_burst(32'hffc,2,4,1,7,0,0,0);read_burst(32'hffc,2,4,1,7);
        write_burst(32'h40,3,4,1,8,0,0,0);read_burst(32'h40,3,4,1,8);
        write_burst(32'h40,2,4,3,9,0,0,0);read_burst(32'h40,2,4,3,9);
        write_burst(32'h40,2,3,2,10,0,0,0);read_burst(32'h40,2,3,2,10);
        write_burst(32'h43,2,4,2,11,0,0,0);read_burst(32'h43,2,4,2,11);
        write_burst(32'h40,2,17,0,12,0,0,0);read_burst(32'h40,2,17,0,12);
        // Independent AXI directions overlap while backend and receivers stall.
        fork
            begin:writer integer j;for(j=0;j<24;j=j+1)write_burst(32'h6000+32'(j*32),j%3,8,1,j,j%7-3,0,0);end
            begin:reader integer j;for(j=0;j<24;j=j+1)read_burst(32'h8000+32'(j*32),j%3,8,1,j);end
        join
        drain();
        random_ready=0;s_axi_bready=0;s_axi_rready=0;
        write_burst(32'h100,2,4,1,1,0,0,0);read_burst(32'h200,2,4,1,2);
        wait(s_axi_bvalid && s_axi_rvalid);repeat(35)@(negedge clk);restart();
        random_ready=1;write_burst(32'h40,2,4,1,15,0,0,0);read_burst(32'h60,2,4,1,14);drain();
        // One early W beat must disappear on reset instead of pairing with fresh AW.
        @(negedge clk);s_axi_wdata=32'hdeadbeef;s_axi_wstrb=15;s_axi_wlast=1;s_axi_wvalid=1;
        do @(posedge clk);while(!s_axi_wready);
        @(negedge clk);s_axi_wvalid=0;restart();
        write_burst(32'h80,2,1,1,13,-4,0,0);read_burst(32'h80,2,1,1,13);drain();
        if(checks<1000 || b_stalls<35 || r_stalls<35)$fatal(1,"AXI4 coverage missing %0d",checks);
        $display("PASS axi4 seed=%0d checks=%0d, FIXED/INCR/WRAP, 256 beats, narrow/unaligned, IDs, errors, stalls, reset",SEED,checks);$finish;
    end
    initial begin #2000000;$fatal(1,"AXI4 timeout B=%0d/%0d R=%0d/%0d",got_b,plan_b,got_r,plan_r);end
endmodule
