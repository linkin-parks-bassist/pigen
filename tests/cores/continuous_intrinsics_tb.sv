module continuous_intrinsics_tb;
    reg clk=0,reset=1,source_valid=0,destination_ready=0;
    reg[7:0] source=0;
    reg signed [7:0] signed_value=0;
    reg [7:0] unsigned_value=0;
    bit signed [7:0] two_value=0;
    wire [11:0] size_literal,size_named,size_grouped;
    wire [3:0] size_nibble;
    wire signed [15:0] wide_signed,narrow_signed,shifted_signed;
    wire [15:0] wide_unsigned,narrow_unsigned;
    wire signed [15:0] wide_two;
    wire [7:0] concat_joined,concat_truncated,concat_sign_high,concat_sign_low;
    wire [7:0] comb_value,comb_chosen,comb_after,comb_truth;
    wire [7:0] case_results[27:0];
    case_comb selections(.value(source),.signed_value(signed_value),.runtime_label(source[0] ? source : source^8'h5a),.results(case_results));
    wire [7:0] loop_results[13:0];
    loop_comb loops(.value(source),.signed_value(signed_value),.results(loop_results));
    wire [7:0] control_results[9:0];
    wire [15:0] fill_results[11:0];
    fill_casts fills(.value(source),.results(fill_results));
    wire [15:0] compound_fill_results[19:0];
    compound_fills compound_masks(.value(source),.results(compound_fill_results));
    wire [15:0] selected_fill_results[15:0];
    selected_fills selected_masks(.value(source),.results(selected_fill_results));
    wire [15:0] called_fill_results[19:0];
    called_fills called_masks(.value(source),.results(called_fill_results));
    task check_called_fills;
        typedef bit [7:0] two_t;
        logic [7:0] inverted;
        logic single_bit;
        logic [15:0] expected[20];
        inverted='1 ^ source;single_bit='1;
        expected[0]=$signed('1);expected[1]=$unsigned('1);
        expected[2]=$signed('1 ^ source);expected[3]=$unsigned('1 ^ source);
        expected[4]=16'($signed('1 ^ source));expected[5]=16'($unsigned('1 ^ source));
        expected[6]=$clog2('1 ^ 8'h00);expected[7]=$clog2('1);
        expected[8]=$signed({'1,source});expected[9]=$unsigned({'1,source});
        expected[10]=inverted;expected[11]=single_bit;
        expected[12]=$signed(single_bit);expected[13]=$unsigned($signed('1));
        expected[14]=$signed($unsigned('1));expected[15]=$signed(two_t'('1));
        expected[16]=$unsigned(two_t'('x));expected[17]=$signed('1 ^ source) >>> 3;
        expected[18]=$unsigned(inverted[7:4]);expected[19]=$clog2(4'hf);
        #1;
        for(integer i=0;i<20;i++) if(called_fill_results[i]!==expected[i])
            $fatal(1,"called fill %0d source=%h got=%h expected=%h",i,source,called_fill_results[i],expected[i]);
    endtask
    task check_selected_fills;
        typedef bit [1:0][3:0] packed_t;
        typedef bit [15:0] two_t;
        logic [7:0] inverted,masked;
        logic [1:0] bit_mask;
        packed_t packed_mask;
        logic [15:0] expected[16];
        inverted='1 ^ source;masked='1 & source;bit_mask=~'0 ^ 2'b00;
        expected[0]=two_t'(inverted[7:4]);expected[1]=source['1];
        expected[2]=source[('1 & 32'd3):0];expected[3]=source[0 +: ('1 & 32'd3)];
        expected[4]=bit_mask[0];expected[5]=inverted[source[2:0]];
        expected[6]=source[('1 & 32'd7)];expected[7]=('1 & source) ? 16'ha5a5 : 16'h5a5a;
        expected[8]=('1 ^ source) ? source : (source ^ 8'hff);
        expected[9]=('x & source) ? 16'ha5a5 : 16'h5a5a;
        packed_mask=packed_t'('1);expected[10]=packed_mask[source[0]];
        packed_mask=packed_t'(source ^ '1);expected[11]=packed_mask[('1 & 32'd1)];
        expected[12]=inverted[7:('1 & 32'd4)];
        expected[13]='1 ? source : 8'h0;expected[14]='0 ? source : 8'h0;
        expected[15]=masked[3:0] ^ '1;
        #1;
        for(integer i=0;i<16;i++) if(selected_fill_results[i]!==expected[i])
            $fatal(1,"selected fill %0d source=%h got=%h expected=%h",i,source,selected_fill_results[i],expected[i]);
    endtask
    task check_compound_fills;
        typedef bit [15:0] two_t;
        typedef logic [15:0] four_t;
        logic [15:0] expected[20];
        expected[0]='1 ^ source;expected[1]=('1 + source) >> 1;
        expected[2]='1 << source[2:0];expected[3]=source >> '1;
        expected[4]=~'0;expected[5]={'1,source};expected[6]={2{'1,source}};
        expected[7]=16'('1 ^ source);expected[8]=two_t'('x | source);
        expected[9]=('1 == source);expected[10]=(|'1);
        expected[11]=four_t'(8'('1) ^ source);expected[12]=8'('1 + source);
        expected[13]=('1 & source) + 16'h100;
        expected[14]=source[0] ? ('1 ^ source) : ('0 | source);
        expected[15]='1 && source;expected[16]=~(&('1 ^ source));
        expected[17]=('1 >> 1) ^ 16'h1234;
        expected[18]=!'0;expected[19]=!('1 ^ source);
        #1;
        for(integer i=0;i<20;i++) if(compound_fill_results[i]!==expected[i])
            $fatal(1,"compound fill %0d source=%h got=%h expected=%h",i,source,compound_fill_results[i],expected[i]);
    endtask
    task check_fill_casts;
        typedef bit [15:0] two_t;
        typedef logic [15:0] four_t;
        typedef bit signed [15:0] signed_t;
        logic [15:0] expected[12];
        expected[0]=two_t'('1);expected[1]=two_t'('x);expected[2]=four_t'('x);
        expected[3]=8'('1);expected[4]=8'(4'('1));expected[5]=4'(8'('1));
        expected[6]=8'(source[0] ? '1 : '0);
        expected[7]=16'('1);expected[8]=two_t'('z);
        expected[9]=two_t'(source[0] ? '1 : '0);
        expected[10]=four_t'(source[0] ? '1 : '0);
        expected[11]=signed_t'(source[0] ? '1 : 8'sh80);
        #1;
        for(integer i=0;i<12;i++) if(fill_results[i]!==expected[i])
            $fatal(1,"fill cast %0d source=%h got=%h expected=%h",i,source,fill_results[i],expected[i]);
    endtask
    loop_control exits(.value(source),.results(control_results));
    task check_loop_control;
        logic [7:0] expected[10];
        bit active;
        integer total;
        active=1;total=0;
        for(integer i=0;i<8;i++) begin
            if(i==source[2:0]) active=0;
            if(active && i!=3) total+=i;
        end
        expected[0]=8'(total);
        expected[1]=4;expected[2]=15;expected[3]=13;expected[4]=13;
        total=0;active=1;
        for(integer i=1;i<=8;i++) begin
            if(active && i>=2) total++;
            // Native while/do conditions stop on X as well as zero.
            if((i<source[2:0])!==1'b1) active=0;
        end
        expected[5]=8'(total);
        expected[6]=13;expected[7]=1;expected[8]=2;expected[9]=22;
        #1;
        for(integer i=0;i<10;i++) if(control_results[i]!==expected[i])
            $fatal(1,"loop control %0d value=%h got=%h expected=%h",i,source,control_results[i],expected[i]);
    endtask
    task check_loops;
        logic [7:0] expected[14];
        logic [7:0] counter,accumulator,selector;
        bit [7:0] repeat_count, helper;
        helper=source;
        counter=0; accumulator=0;
        while(counter<source[2:0]) begin accumulator+=counter; counter++; end
        expected[0]=accumulator;
        repeat_count=source & 8'd7; accumulator=0;
        repeat(repeat_count) begin accumulator++; repeat_count=0; end
        expected[1]=accumulator;
        counter=0; accumulator=0;
        repeat(3) begin
            logic [7:0] inner;
            inner=0;
            while(inner<2) begin accumulator+=counter; inner++; end
            counter++;
        end
        expected[2]=accumulator;
        accumulator=8'h5a;
        repeat(signed_value) accumulator++;
        expected[3]=accumulator;
        selector=source; accumulator=0;
        while(selector) begin accumulator++; selector=0; end
        expected[4]=accumulator;
        expected[5]=8'd2 ^ helper;
        accumulator=0;
        for(int i=0;i<3;i++) begin
            accumulator+=8'(i);
            for(int i=0;i<2;i++) accumulator++;
        end
        expected[6]=accumulator;
        accumulator=0;
        counter=0;selector=3;
        while(counter<3) begin accumulator+=selector;counter++;selector=3-counter;end
        expected[7]=accumulator;
        accumulator=0;
        begin
            int i,j;
            i=0;j=1;
            while(i<3) begin accumulator+=8'(j);i++;j+=2;end
        end
        expected[8]=accumulator;
        counter=0;accumulator=0;
        while(counter<source[2:0]) begin accumulator++;counter++;end
        expected[9]=accumulator;
        accumulator=8'ha5;
        for(int i=3;i<0;i--) accumulator=0;
        for(counter=0;counter<2;counter++) ;
        expected[10]=accumulator ^ counter;
        accumulator=0;
        for(int i=-2;i<2;i++) accumulator+=8'(i+2);
        expected[11]=accumulator;
        accumulator=0;
        for(counter=0;counter<1 && (signed_value+8'sd1)<8'sd0;counter++) accumulator++;
        expected[12]=accumulator;
        expected[13]=1;


        #1;
        for(integer k=0;k<14;k++) if(loop_results[k]!==expected[k])
            $fatal(1,"loop result %0d value=%h signed=%h got=%h expected=%h",k,source,signed_value,loop_results[k],expected[k]);
    endtask
    task check_cases;
        logic [7:0] reference_results[28];
        logic [7:0] selector;
        bit [7:0] helper;
        selector=source; helper=source;
        case(selector[0])
            default: reference_results[0]=8'hcc;
            1'b1: begin selector[0]=0; reference_results[0]=8'ha5; end
            1'b0: reference_results[0]=8'h5a;
        endcase
        reference_results[1]=selector;
        case(source[1:0])
            2'b00,2'b01: begin
                logic [7:0] selector;
                selector=8'h13;
                case(source[2])
                    1'b1: selector+=8'd7;
                    default selector-=8'd3;
                endcase
                reference_results[2]=selector;
            end
            default: reference_results[2]=8'h72;
        endcase
        case(source)
            (source[0] ? source : source^8'h5a): reference_results[3]=8'h11;
            8'hff: reference_results[3]=8'h22;
            default: reference_results[3]=8'h33;
        endcase
        case(signed_value)
            16'h0080: reference_results[4]=8'h44;
            default: reference_results[4]=8'h45;
        endcase
        case(signed_value)
            16'shff80: reference_results[5]=8'h55;
            default: reference_results[5]=8'h56;
        endcase
        case(signed_value)
            128: reference_results[6]=8'h66;
            default: reference_results[6]=8'h67;
        endcase
        reference_results[7]=8'h70;
        case(source)
            (source[0] ? 8'h01 : 8'h02),8'h03: reference_results[7]=8'h77;
        endcase
        reference_results[8]=helper;
        case(signed_value+8'sd1) 8'sh80: reference_results[9]=8'h91; default: reference_results[9]=8'h92; endcase
        case(signed_value+8'sd1) 16'sd128: reference_results[10]=8'ha1; default: reference_results[10]=8'ha2; endcase
        case(signed_value>>>1) 16'h0040: reference_results[11]=8'hb1; default: reference_results[11]=8'hb2; endcase
        case(source) '1: reference_results[12]=8'hc1; default: reference_results[12]=8'hc2; endcase
        case(source) '0: reference_results[13]=8'hd1; default: reference_results[13]=8'hd2; endcase
        case(8'('1)) 8'hff: reference_results[14]=8'he1; default: reference_results[14]=8'he2; endcase
        case((signed_value+8'sd1)==16'sd128) 1'b1: reference_results[15]=8'hf1; default: reference_results[15]=8'hf2; endcase
        case({4'('1),4'(source)}) 8'hfa: reference_results[16]=8'h81; default: reference_results[16]=8'h82; endcase
        case(source[0] ? '1 : 8'h12) 8'hff: reference_results[17]=8'h83; default: reference_results[17]=8'h84; endcase
        case((source+8'd123)/8'd3) 8'd0: reference_results[18]=8'h85; default: reference_results[18]=8'h86; endcase
        casez(source) 8'b0???????: reference_results[19]=8'h87; 8'b1???????: reference_results[19]=8'h88; default: reference_results[19]=8'h89; endcase
        reference_results[20]=source; case(source) default: reference_results[20]+=8'd17; endcase
        case({1'b0,helper}+9'd1) 9'd256: reference_results[21]=8'h8a; default: reference_results[21]=8'h8b; endcase
        case(64'd2147483648) 64'd2147483648: reference_results[22]=8'h8c; default: reference_results[22]=8'h8d; endcase
        case({1'b0,helper}<<1) 9'd256: reference_results[23]=8'h8e; default: reference_results[23]=8'h8f; endcase
        case({1'b0,helper}+9'd1) 9'd256: reference_results[24]=8'h90; default: reference_results[24]=8'h91; endcase
        case(8'd1) 8'd1: reference_results[25]=8'h92; default: reference_results[25]=8'h93; endcase
        case(bit'(1'bx)) 1'b0: reference_results[26]=8'h94; default: reference_results[26]=8'h95; endcase
        case(bit'(!1'bx)) 1'b0: reference_results[27]=8'h96; default: reference_results[27]=8'h97; endcase
        #1;
        for(integer j=0;j<28;j=j+1)
            if(case_results[j]!==reference_results[j])
                $fatal(1,"case item %0d mismatch for %h: %h expected %h",j,source,case_results[j],reference_results[j]);
    endtask
    wire bit [15:0] cast_u;
    wire bit signed [15:0] cast_s;
    wire bit [7:0] cast_composite;
    wire bit cast_direct;
    wire cast_equal;
    wire bit [127:0] cast_wide;
    state_casts conversions(.signed_value(signed_value),.unsigned_value(unsigned_value),.*);
    task check_state_casts;
        bit [15:0] ref_u;
        bit signed [15:0] ref_s;
        bit [7:0] ref_composite,cleared;
        bit ref_direct;
        bit [127:0] ref_wide;
        ref_u=unsigned_value;ref_s=signed_value;cleared=signed_value;
        ref_composite=cleared^8'd1;ref_direct=signed_value[0];ref_wide=signed_value;
        #1;
        if(cast_u!==ref_u || cast_s!==ref_s || cast_composite!==ref_composite ||
           cast_direct!==ref_direct || cast_equal!==(cleared==8'd1) || cast_wide!==ref_wide)
            $fatal(1,"four-state to two-state conversion was delayed past an operation");
    endtask
    wire [7:0] numeric_observed;
    wire [7:0] compound_results[21:0];
    wire [7:0] compound_numeric,compound_selected;
    compound_comb updates(.value(source),.signed_value(signed_value),.pick(source[0]),
        .results(compound_results),.numeric(compound_numeric),.selected(compound_selected));
    task check_updates;
        logic [7:0] reference_results[22];
        logic signed [7:0] signed_work;
        bit [7:0] number;
        logic [3:0] high,low;
        logic [7:0] entries[2];
        bit slot;
        reference_results[0]=source; reference_results[0]+=8'd19;
        reference_results[1]=source; reference_results[1]-=8'd29;
        reference_results[2]=source; reference_results[2]*=8'd13;
        reference_results[3]=source; reference_results[3]/=8'd3;
        reference_results[4]=source; reference_results[4]%=8'd7;
        reference_results[5]=source; reference_results[5]&=8'hb6;
        reference_results[6]=source; reference_results[6]|=8'h49;
        reference_results[7]=source; reference_results[7]^=8'hc3;
        reference_results[8]=source; reference_results[8]<<=source[2:0];
        reference_results[9]=source; reference_results[9]>>=source[2:0];
        reference_results[10]=source; reference_results[10]<<<=source[2:0];
        reference_results[11]=source; reference_results[11]>>>=source[2:0];
        signed_work=signed_value; signed_work>>>=3; reference_results[12]=signed_work;
        signed_work=signed_value; signed_work/=-8'sd3; reference_results[13]=signed_work;
        reference_results[14]=source; ++reference_results[14]; reference_results[14]++; --reference_results[14]; reference_results[14]--;
        high=source[7:4]; low=source[3:0]; {high,low}+=8'd23; reference_results[15]={high,low};
        reference_results[16]=source; reference_results[16]+=16'hff01;
        // Icarus truncates a wider compound divisor prematurely. Use the
        // IEEE 1800-2017 11.4.1 equivalent expanded assignment here.
        reference_results[17]=8'(source/16'h0101);
        signed_work=signed_value; signed_work*=16'sd131; reference_results[18]=signed_work;
        signed_work=signed_value; signed_work/=16'd3; reference_results[19]=signed_work;
        signed_work=signed_value; signed_work/=source; reference_results[20]=signed_work;
        signed_work=signed_value; signed_work%=16'd7; reference_results[21]=signed_work;
        number=source; number--; ++number; number++; ++number; number-=2;
        slot=source[0]; entries[0]=source; entries[1]=~source;
        entries[slot]+=source; ++slot; entries[slot]^=source;
        #1;
        for(integer j=0;j<22;j=j+1)
            if(compound_results[j]!==reference_results[j])
                $fatal(1,"compound operation %0d mismatch for %h: %h expected %h",j,source,compound_results[j],reference_results[j]);
        if(compound_numeric!==number || compound_selected!==(entries[0]^entries[1]))
            $fatal(1,"compound numerical wrap or dynamic lvalue order changed");
    endtask
    wire source_ready,destination_valid,source_offered,destination_accepting,missing,fire,below,observed,case_match;
    wire[7:0] destination,raw,selected,plus_one,repeated,shifted;
    wire[1:0] flags,split_flags;
    wire[3:0] low_nibble,cast_nibble;
    reg[7:0] held;
    reg was_stalled=0;
    integer accepted=0,received=0;
    reg[7:0] expected[0:2047];
    continuous_intrinsics dut(.*);
    static_intrinsics static_dut(.value(source),.observed(observed));
    numeric_comb numeric_dut(.value(source),.observed(numeric_observed));
    always #5 clk=~clk;
    always @(posedge clk) if(!reset) begin
        if(source_valid && source_ready) begin expected[accepted]=source;accepted=accepted+1;end
        if(was_stalled && (!destination_valid || destination!==held)) $fatal(1,"observers changed buffered ownership");
        if(destination_valid && destination_ready) begin
            if(received>=accepted || destination!==expected[received]) $fatal(1,"continuous observer consumed a token");
            received=received+1;
        end
        held=destination;was_stalled=destination_valid && !destination_ready;
    end
    task check_comb;
        reg ready_now;
        reg[7:0] sum;
        #1;
        ready_now=!destination_valid || destination_ready;sum=source+8'd1;
        if(source_offered!==source_valid || destination_accepting!==ready_now || missing!==!source_valid ||
           fire!==(source_valid && ready_now) || raw!==source || selected!==(source_valid?source:8'hcc) ||
           repeated!=={8{source_valid}} || shifted!==(source>>2) || cast_nibble!==source[3:0] ||
           case_match!==(source===8'ha5) || plus_one!==sum || flags!=={ready_now,source_valid} || split_flags!==flags ||
           below!==(source_valid && source<=8'h7f) || observed!==0 || low_nibble!==source[3:0])
            $fatal(1,"continuous intrinsic value/latency at byte %h",source);
        if(size_literal!=={4'b0,source} || size_named!==size_literal || size_grouped!==size_literal ||
           size_nibble!==source[3:0]) $fatal(1,"intrinsic size cast value/latency");
        if(concat_joined!==source || concat_truncated!==8'ha5)
            $fatal(1,"continuous concat ordering/truncation");
        if(comb_value!=={source[7:1],1'b0} || comb_after!==comb_value ||
           comb_chosen!==(source[0] ? 8'ha5 : 8'h5a) || numeric_observed!==source)
            $fatal(1,"always_comb blocking order or branch entry changed");
    endtask
    task check_sizes;
        reg signed [15:0] expected_signed,expected_narrow,expected_shift,expected_two;
        reg [15:0] expected_unsigned,expected_unsigned_narrow;
        reg [7:0] expected_truth;
        expected_signed=12'(signed_value); expected_unsigned=12'(unsigned_value);
        expected_narrow=4'(signed_value); expected_unsigned_narrow=4'(unsigned_value);
        expected_shift=12'(signed_value) >>> 3; expected_two=12'(two_value);
        if(signed_value) expected_truth=8'hcc; else expected_truth=8'h33;
        #1;
        if(wide_signed!==expected_signed || wide_unsigned!==expected_unsigned ||
           narrow_signed!==expected_narrow || narrow_unsigned!==expected_unsigned_narrow ||
           shifted_signed!==expected_shift || wide_two!==expected_two ||
           concat_sign_high!=={8{signed_value[7]}} || concat_sign_low!==signed_value || comb_truth!==expected_truth)
            $fatal(1,"SV size cast signedness/state mismatch at %h/%h",signed_value,unsigned_value);
    endtask
    initial begin
        repeat(4) @(negedge clk);reset=0;
        for(integer k=0;k<1024;k=k+1) begin
            source=k[7:0];signed_value=k[7:0];unsigned_value=k[7:0];two_value=k[7:0];
            source_valid=k[0];destination_ready=k[2];check_comb();check_sizes();check_updates();check_cases();check_loops();check_loop_control();check_state_casts();check_fill_casts();check_compound_fills();check_selected_fills();check_called_fills();
            @(negedge clk);
        end
        source_valid=0;destination_ready=1;repeat(4) @(negedge clk);
        signed_value=8'bx01z10x1;unsigned_value=8'bz10x01z0;check_sizes();check_state_casts();
        signed_value=8'bz01x10z1;unsigned_value=8'bx10z01x0;check_sizes();check_state_casts();
        signed_value=8'bx0000000;check_sizes();check_state_casts();
        signed_value=8'bxxxx0001;check_sizes();check_state_casts();
        source=8'bx01z10x1;check_updates();check_cases();check_loops();check_loop_control();
        source=8'bz10x01z0;check_updates();check_cases();check_loops();check_loop_control();
        source=8'hxx;check_cases();check_loops();check_loop_control();check_fill_casts();check_compound_fills();check_selected_fills();check_called_fills();source=8'hzz;check_cases();check_loops();check_loop_control();check_fill_casts();check_compound_fills();check_selected_fills();check_called_fills();
        if(accepted!=received) $fatal(1,"observer token count");
        $display("PASS continuous intrinsics: ready/valid/accepts/peek, unary, arithmetic, concat, ternary, slice; no token consumption");$finish;
    end
    initial begin #40000;$fatal(1,"continuous intrinsic timeout");end
endmodule
