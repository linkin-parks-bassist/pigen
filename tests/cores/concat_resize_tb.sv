module concat_resize_tb;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [7:0] value=0;
    reg [3:0] source_valid=0,upper_ready=0,lower_ready=0;
    wire [3:0] source_ready,upper_valid,lower_valid;
    wire [3:0][7:0] upper,lower;
    concat_narrow m0(.clk(clk),.reset(reset),.source(value),.source_valid(source_valid[0]),.source_ready(source_ready[0]),
        .upper(upper[0]),.upper_valid(upper_valid[0]),.upper_ready(upper_ready[0]),.lower(lower[0]),.lower_valid(lower_valid[0]),.lower_ready(lower_ready[0]));
    concat_wide m1(.clk(clk),.reset(reset),.source(value),.source_valid(source_valid[1]),.source_ready(source_ready[1]),
        .upper(upper[1]),.upper_valid(upper_valid[1]),.upper_ready(upper_ready[1]),.lower(lower[1]),.lower_valid(lower_valid[1]),.lower_ready(lower_ready[1]));
    concat_signed m2(.clk(clk),.reset(reset),.source(value),.source_valid(source_valid[2]),.source_ready(source_ready[2]),
        .upper(upper[2]),.upper_valid(upper_valid[2]),.upper_ready(upper_ready[2]),.lower(lower[2]),.lower_valid(lower_valid[2]),.lower_ready(lower_ready[2]));
    concat_placeholder_resize m3(.clk(clk),.reset(reset),.source(value),.source_valid(source_valid[3]),.source_ready(source_ready[3]),
        .upper(upper[3]),.upper_valid(upper_valid[3]),.upper_ready(upper_ready[3]),.lower(lower[3]),.lower_valid(lower_valid[3]),.lower_ready(lower_ready[3]));
    reg [15:0] expected[0:3][0:1023];
    integer sent[0:3],got_hi[0:3],got_lo[0:3];
    reg [3:0] accepted=0,sh=0,sl=0;
    reg [3:0][7:0] held_hi,held_lo;
    integer i,n,checks=0,cycles=0,trace_file,checks_file;
    reg [31:0] rng=32'hbada5531;
    reg drain=0;
    always @(negedge clk)begin
        rng=rng*1664525+1013904223;
        upper_ready=drain?15:rng[3:0];lower_ready=drain?15:rng[7:4];
    end
    always @(posedge clk)begin
        cycles=cycles+1;
        if(reset)begin for(i=0;i<4;i=i+1)begin sent[i]=0;got_hi[i]=0;got_lo[i]=0;end end
        else begin
            accepted=source_valid & source_ready;
            for(i=0;i<4;i=i+1)begin
                if(sh[i] && (!upper_valid[i] || upper[i]!==held_hi[i]))$fatal(1,"concat upper changed during stall");
                if(sl[i] && (!lower_valid[i] || lower[i]!==held_lo[i]))$fatal(1,"concat lower changed during stall");
                if(accepted[i])begin
                    if(i==0 || i==3)expected[i][sent[i]]={8'(value+8'd1),value};
                    else if(i==1)expected[i][sent[i]]={8'b0,value};
                    else expected[i][sent[i]]={{8{value[7]}},value};
                    sent[i]=sent[i]+1;
                end
                if(upper_valid[i] && upper_ready[i])begin
                    if(got_hi[i]>=sent[i] || upper[i]!==expected[i][got_hi[i]][15:8])$fatal(1,"concat upper %0d index %0d got %h expected %h",i,got_hi[i],upper[i],expected[i][got_hi[i]][15:8]);
                    $fwrite(checks_file,"%0d,%0d,%0d\n",checks,expected[i][got_hi[i]][15:8],upper[i]);checks=checks+1;got_hi[i]=got_hi[i]+1;
                end
                if(lower_valid[i] && lower_ready[i])begin
                    if(got_lo[i]>=sent[i] || lower[i]!==expected[i][got_lo[i]][7:0])$fatal(1,"concat lower %0d index %0d",i,got_lo[i]);
                    $fwrite(checks_file,"%0d,%0d,%0d\n",checks,expected[i][got_lo[i]][7:0],lower[i]);checks=checks+1;got_lo[i]=got_lo[i]+1;
                end
            end
            sh=upper_valid & ~upper_ready;sl=lower_valid & ~lower_ready;held_hi=upper;held_lo=lower;
        end
        $fwrite(trace_file,"%0d,%0d,%0d,%0d,%0d\n",cycles,source_valid[0],source_ready[0],upper_valid[0],upper_ready[0]);
    end
    initial begin
        trace_file=$fopen("trace.csv","w");$fwrite(trace_file,"cycle,in_valid,in_ready,out_valid,out_ready\n");
        checks_file=$fopen("checks.csv","w");$fwrite(checks_file,"index,expected,observed\n");
        $dumpfile("wave.vcd");$dumpvars(0,concat_resize_tb);
        repeat(3)@(negedge clk);reset=0;
        for(n=0;n<1024;n=n+1)begin
            @(negedge clk);value=8'(n);source_valid=15;
            while(source_valid!=0)begin
                @(negedge clk);source_valid=source_valid & ~accepted;
            end
        end
        drain=1;wait(got_hi[0]==1024 && got_hi[1]==1024 && got_hi[2]==1024 && got_hi[3]==1024 &&
                     got_lo[0]==1024 && got_lo[1]==1024 && got_lo[2]==1024 && got_lo[3]==1024);
        @(negedge clk);$display("PASS concatenation resize: 8192 byte comparisons, truncation, zero/sign extension, placeholder widths and independent stalls");$finish;
    end
    initial begin #500000;$fatal(1,"concat resize timeout");end
endmodule
