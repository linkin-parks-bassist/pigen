module axil_tb;
    parameter integer SEED=1;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [31:0] s_axi_awaddr=0,s_axi_wdata=0,s_axi_araddr=0;
    reg [2:0] s_axi_awprot=0,s_axi_arprot=0;
    reg [3:0] s_axi_wstrb=0;
    reg s_axi_awvalid=0,s_axi_wvalid=0,s_axi_arvalid=0;
    reg s_axi_bready=0,s_axi_rready=0;
    wire s_axi_awready,s_axi_wready,s_axi_bvalid,s_axi_arready,s_axi_rvalid;
    wire [1:0] s_axi_bresp,s_axi_rresp;
    wire [31:0] s_axi_rdata;
    pigen_axil_registers dut(.*);
    reg [31:0] model[0:3],aq[0:4095],dq[0:4095];
    reg [3:0] sq[0:4095];
    reg [1:0] bq[0:4095];
    reg [33:0] rq[0:4095];
    integer na=0,nw=0,np=0,nb=0,nar=0,nr=0,checks=0,cycle=0;
    integer bstalls=0,rstalls=0,awfirst=0,wfirst=0,both=0;
    reg bs=0,rs=0;
    reg [1:0] bh;
    reg [33:0] rh;
    integer i,j,trace_file,checks_file;
    reg [31:0] rng=SEED;
    function [31:0] random_word(input [31:0] x);
        random_word=x*1664525+1013904223;
    endfunction
    always @(posedge clk) begin
        cycle=cycle+1;
        if(reset) begin
            na=0;nw=0;np=0;nb=0;nar=0;nr=0;bs=0;rs=0;
            for(i=0;i<4;i=i+1)model[i]=0;
        end else begin
            if(bs && (!s_axi_bvalid || s_axi_bresp!==bh))$fatal(1,"B changed under backpressure");
            if(rs && (!s_axi_rvalid || {s_axi_rresp,s_axi_rdata}!==rh))$fatal(1,"R changed under backpressure");
            if(s_axi_awvalid && s_axi_awready) begin aq[na]=s_axi_awaddr;na=na+1;end
            if(s_axi_wvalid && s_axi_wready) begin dq[nw]=s_axi_wdata;sq[nw]=s_axi_wstrb;nw=nw+1;end
            if(na>np && nw>np)begin
                bq[np]=(aq[np]<16 && aq[np][1:0]==0)?0:3;
                if(bq[np]==0)for(j=0;j<4;j=j+1)
                    if(sq[np][j]) model[aq[np]/4][j*8+:8]=dq[np][j*8+:8];
                np=np+1;
            end
            if(s_axi_bvalid && s_axi_bready)begin
                if(nb>=np || s_axi_bresp!==bq[nb])$fatal(1,"write response %0d got %0d expected %0d",nb,s_axi_bresp,bq[nb]);
                $fwrite(checks_file,"%0d,%0d,%0d\n",checks,bq[nb],s_axi_bresp);checks=checks+1;nb=nb+1;
            end
            // Reads occur after writes drain, or concurrently to an untouched word.
            if(s_axi_arvalid && s_axi_arready)begin
                rq[nar]=(s_axi_araddr<16 && s_axi_araddr[1:0]==0)?{2'b00,model[s_axi_araddr/4]}:{2'b11,32'b0};
                nar=nar+1;
            end
            if(s_axi_rvalid && s_axi_rready)begin
                if(nr>=nar || {s_axi_rresp,s_axi_rdata}!==rq[nr])$fatal(1,"read response %0d got %h expected %h",nr,{s_axi_rresp,s_axi_rdata},rq[nr]);
                $fwrite(checks_file,"%0d,%0d,%0d\n",checks,rq[nr],{s_axi_rresp,s_axi_rdata});checks=checks+1;nr=nr+1;
            end
            bs=s_axi_bvalid && !s_axi_bready;bh=s_axi_bresp;
            rs=s_axi_rvalid && !s_axi_rready;rh={s_axi_rresp,s_axi_rdata};
            if(bs)bstalls=bstalls+1;if(rs)rstalls=rstalls+1;
        end
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",cycle,s_axi_awvalid,s_axi_awready,s_axi_wvalid,s_axi_wready,s_axi_bvalid,s_axi_bready,s_axi_arvalid,s_axi_arready,s_axi_rvalid,s_axi_rready,reset);
    end
    task address(input [31:0] a,input integer delay_cycles);
        repeat(delay_cycles)@(negedge clk);
        @(negedge clk);s_axi_awaddr=a;s_axi_awvalid=1;
        do @(posedge clk);while(!s_axi_awready);
        @(negedge clk);s_axi_awvalid=0;
    endtask
    task data(input [31:0] d,input [3:0] st,input integer delay_cycles);
        repeat(delay_cycles)@(negedge clk);
        @(negedge clk);s_axi_wdata=d;s_axi_wstrb=st;s_axi_wvalid=1;
        do @(posedge clk);while(!s_axi_wready);
        @(negedge clk);s_axi_wvalid=0;
    endtask
    task write_word(input [31:0] a,input [31:0] d,input [3:0] st,input integer skew);
        if(skew>0)awfirst=awfirst+1;else if(skew<0)wfirst=wfirst+1;else both=both+1;
        fork address(a,skew<0?-skew:0);data(d,st,skew>0?skew:0);join
    endtask
    task read_word(input [31:0] a);
        @(negedge clk);s_axi_araddr=a;s_axi_arvalid=1;
        do @(posedge clk);while(!s_axi_arready);
        @(negedge clk);s_axi_arvalid=0;
    endtask
    task drain;
        @(negedge clk);s_axi_bready=1;s_axi_rready=1;
        wait(nb==na && nr==nar);repeat(4)@(negedge clk);
    endtask
    integer k,mask;
    reg random_ready=0;
    always @(negedge clk)if(random_ready)begin
        rng=random_word(rng);s_axi_bready=rng[2:0]>2;s_axi_rready=rng[6:4]>3;
    end
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,aw_valid,aw_ready,w_valid,w_ready,b_valid,b_ready,ar_valid,ar_ready,r_valid,r_ready,reset\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,axil_tb);
        repeat(4)@(negedge clk);reset=0;
        // All byte masks at all words; skew covers AW-before-W and W-before-AW.
        random_ready=1;
        for(k=0;k<4;k=k+1)for(mask=0;mask<16;mask=mask+1)begin
            write_word(k*4,32'h926ac531 ^ (32'h1234567*mask),mask[3:0],(mask%3-1)*5);
            wait(nb==na);read_word(k*4);wait(nr==nar);
        end
        // Continuous writes with queued responses, concurrent reads to untouched word3.
        fork
            begin for(k=0;k<180;k=k+1)write_word((k%3)*4,32'h13579bdf*k,4'(k),k%7-3);end
            begin repeat(100)read_word(12);end
        join
        random_ready=0;drain();
        for(k=0;k<4;k=k+1)read_word(k*4);drain();
        for(k=1;k<20;k=k+1)if(k%4!=0)begin write_word(k,32'hffffffff,15,2);read_word(k);end
        write_word(16,32'hfedcba98,15,0);read_word(16);
        write_word(32'hfffffff0,1,15,-5);read_word(32'hfffffff0);drain();
        // Hold B and R simultaneously, verify stable responses, then reset in flight.
        @(negedge clk);s_axi_bready=0;s_axi_rready=0;
        write_word(0,32'hcafebabe,15,0);wait(s_axi_bvalid);read_word(4);wait(s_axi_rvalid);
        repeat(40)@(negedge clk);
        // Orphan AW and pending responses must be discarded by reset.
        address(8,0);@(negedge clk);reset=1;
        repeat(3)@(negedge clk);reset=0;s_axi_bready=1;s_axi_rready=1;
        repeat(10)@(negedge clk);
        if(s_axi_bvalid || s_axi_rvalid)$fatal(1,"stale response after reset");
        for(k=0;k<4;k=k+1)read_word(k*4);drain();
        write_word(4,32'h89abcdef,15,-6);drain();read_word(4);drain();
        // Reset with only W buffered; a fresh AW must not pair with stale data.
        data(32'hdeadbeef,15,0);@(negedge clk);reset=1;
        repeat(3)@(negedge clk);reset=0;
        address(0,0);repeat(12)@(negedge clk);
        if(s_axi_bvalid)$fatal(1,"stale W paired after reset");
        data(32'h10203040,15,0);drain();read_word(0);drain();
        if(checks<400 || bstalls<40 || rstalls<40 || awfirst<10 || wfirst<10 || both<10)$fatal(1,"coverage missing");
        $display("PASS axil seed=%0d checks=%0d AW-first=%0d W-first=%0d together=%0d B-stalls=%0d R-stalls=%0d",SEED,checks,awfirst,wfirst,both,bstalls,rstalls);$finish;
    end
    initial begin #1000000;$fatal(1,"AXIL timeout na=%0d nw=%0d nb=%0d nar=%0d nr=%0d",na,nw,nb,nar,nr);end
endmodule
