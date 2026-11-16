#!/usr/bin/env python3
"""Independent five-channel transaction scoreboard for address switching."""
from pathlib import Path
import shutil
import tempfile
from native_protocol_test import ROOT, run


def case(directory,count,full,address_width,seed=1,native=False,native_mode='boundary'):
    directory.mkdir()
    a=address_width
    aw=a+40 if full else a+3
    ww,bw,rw=(79,23,91) if full else (36,2,34)
    alsb,ids,blsb,rlsb,last,wl=(8,5,5,69,71,72) if full else (0,0,0,32,0,0)
    lengths=a+8 if full else 0
    base=1<<(a-1)
    bases='{'+','.join(f"{a}'h{base+i*65536:x}" for i in reversed(range(count)))+'}'
    limits='{'+','.join(f"{a}'h{base+i*65536+4095:x}" for i in reversed(range(count)))+'}'
    channels=[('aw',aw,True),('w',ww,True),('b',bw,False),('ar',aw,True),('r',rw,False)]
    declarations=[];ports=[]
    for side in ('s','m'):
        for channel,width,forward in channels:
            width*=count if side=='m' else 1
            valid_width=count if side=='m' else 1
            incoming=forward if side=='s' else not forward
            for field,bits,is_input in [('payload',width,incoming),('valid',valid_width,incoming),('ready',valid_width,not incoming)]:
                name=f'{side}_{channel}_{field}'
                declarations.append(f'logic [{bits-1}:0] {name}'+("='0;" if is_input else ';'))
                ports.append(f'{"input" if is_input else "output"} logic [{bits-1}:0] {name}')
    wrapper='module dut(input logic clk,reset,'+','.join(ports)+');\n'
    wrapper+=f'''pigen_axi_demux #(.N({count}),.AW_W({aw}),.W_W({ww}),.B_W({bw}),.AR_W({aw}),.R_W({rw}),
.ADDR_W({a}),.AW_ADDR_LSB({alsb}),.AR_ADDR_LSB({alsb}),.FULL({int(full)}),.WLAST_BIT({wl}),.RLAST_BIT({last}),.ARLEN_LSB({lengths}),
.ID_W({ids}),.R_ID_LSB({64 if full else 0}),.B_RESP_LSB({blsb}),.R_RESP_LSB({rlsb}),.BASES({bases}),.LIMITS({limits})) core(.*);
endmodule
'''
    header=f'''module tb;
localparam N={count},FULL={int(full)},A={a},AW={aw},WW={ww},BW={bw},RW={rw},ALSB={alsb},IW={ids},BR={blsb},RR={rlsb},RID={64 if full else 0},LAST={last},WLAST={wl},LEN={lengths},TXNS=64;
localparam [A-1:0] BASE={a}'h{base:x};
logic clk=0,reset=1;integer epoch=0,cycles=0,aw_sent=0,ar_sent=0,w_tx=0,w_beat=0,b_received=0,r_tx=0,r_beat=0,r_words=0;
integer early_data=0,early_reads=0,overlap=0,error_write_256=0,error_read_256=0;
integer lane_tx[N],w_count[N],read_tx[N],read_beat[N],next_aw[N],next_ar[N];
logic [N-1:0] aw_seen='0,read_active='0,aw_stall='0,w_stall='0,ar_stall='0;
logic [AW-1:0] held_aw[N],held_ar[N];logic [WW-1:0] held_w[N],queued_w[N][0:511];
logic aw_accepted=0,w_accepted=0,ar_accepted=0;logic [N-1:0] b_accepted='0,r_accepted='0;
logic b_stall=0,r_stall=0;logic [BW-1:0] held_b;logic [RW-1:0] held_r;
{chr(10).join(declarations)}
dut device(.*);
always #5 clk=~clk;
function automatic integer route(input integer tx,reading);route=(tx+reading)%(N+1);endfunction
function automatic integer burst(input integer tx,reading);burst=(tx/2+reading)%3;endfunction
function automatic integer beats(input integer tx,reading);
begin
if(!FULL) beats=1;
else if(burst(tx,reading)==2) beats=2<<((tx/3)%4);
else if(burst(tx,reading)==1 && tx%7==reading) beats=256;
else beats=tx%5+1;
end endfunction
function automatic integer offset(input integer tx,reading);offset=FULL && burst(tx,reading)==0 && tx%5==0 ? 4095 : (tx%16)*4;endfunction
function automatic [AW-1:0] request(input integer tx,reading,phase);
reg [AW-1:0] value;reg [A-1:0] address;
begin
for(integer b=0;b<AW;b++) value[b]=1'((tx*(b+3)+reading*19+b)>>(b%5));
address=BASE+A'(route(tx,reading)*65536+offset(tx,reading));
if(route(tx,reading)==N && (tx/(N+1))%2==0) address=A'(offset(tx,reading));
value[ALSB+:A]=address;
for(integer b=0;b<IW;b++) value[b]=1'((tx*3+reading)>>b);
if(FULL) begin
for(integer b=0;b<8;b++) value[LEN+b]=1'((beats(tx,reading)-1)>>b);
value[LEN+8+:3]=0; // Narrow byte transfer; all generated bursts stay within 4 KiB.
value[LEN+11+:2]=2'(burst(tx,reading));
value[LEN+13]=0;value[LEN+14+:4]=0;
end
if(phase==0) value[AW-1]=~value[AW-1];
request=value;
end endfunction
function automatic [WW-1:0] word(input integer tx,beat,phase);
reg [WW-1:0] value;
begin
for(integer b=0;b<WW;b++) value[b]=1'((tx*(b+3)+beat*(b+7)+b)>>(b%5));
if(FULL) begin
integer address;
address=offset(tx,0);
if(burst(tx,0)==1) address=address+beat;
if(burst(tx,0)==2) address=(address/beats(tx,0))*beats(tx,0)+(address%beats(tx,0)+beat)%beats(tx,0);
value[64+:8]=8'(1<<(address%8));
value[WLAST]=beat==beats(tx,0)-1;
end
if(phase==0) value[WW-1]=~value[WW-1];
word=value;
end endfunction
function automatic [BW-1:0] response_b(input integer tx,error_response);
reg [BW-1:0] value;
begin
value='0;
if(!error_response) for(integer b=0;b<BW;b++) value[b]=1'((tx*(b+11)+b)>>(b%5));
value[BR+:2]=error_response ? 2'b11 : tx%4==0 ? 2'b10 : 2'b00;
for(integer b=0;b<IW;b++) value[b]=1'((tx*3)>>b);
response_b=value;
end endfunction
function automatic [RW-1:0] response_r(input integer tx,beat,error_response);
reg [RW-1:0] value;
begin
value='0;
if(!error_response) for(integer b=0;b<RW;b++) value[b]=1'((tx*(b+13)+beat*(b+17)+b)>>(b%5));
value[RR+:2]=error_response ? 2'b11 : tx%4==0 ? 2'b10 : 2'b00;
for(integer b=0;b<IW;b++) value[RID+b]=1'((tx*3+1)>>b);
if(FULL) value[LAST]=beat==beats(tx,1)-1;
response_r=value;
end endfunction
'''
    drive='''always @(negedge clk) begin
if(reset) begin
s_aw_valid=0;s_w_valid=0;s_ar_valid=0;s_b_ready=0;s_r_ready=0;
m_aw_ready='0;m_w_ready='0;m_ar_ready='0;m_b_valid='0;m_r_valid='0;
end else begin
if(!s_aw_valid || aw_accepted) begin s_aw_valid=aw_sent<TXNS && cycles>8 && (cycles%7)<5;s_aw_payload=request(aw_sent,0,epoch);end
if(!s_w_valid || w_accepted) begin s_w_valid=w_tx<TXNS && (w_tx%3!=1 || aw_sent>w_tx) && (cycles%9)<7;s_w_payload=word(w_tx,w_beat,epoch);end
if(!s_ar_valid || ar_accepted) begin s_ar_valid=ar_sent<TXNS && (cycles%5)!=0;s_ar_payload=request(ar_sent,1,epoch);end
s_b_ready=epoch==1 && cycles>90 && ((cycles+SEED)%17)>6;
s_r_ready=epoch==1 && cycles>20 && ((cycles+SEED)%19)>4;
for(integer lane=0;lane<N;lane++) begin
m_aw_ready[lane]=epoch==1 && cycles>80 && ((cycles+lane+SEED)%7)>1;
m_w_ready[lane]=epoch==1 && ((cycles*3+lane+SEED)%11)>2;
m_ar_ready[lane]=epoch==1 && ((cycles+lane+SEED)%5)>0;
if(!m_b_valid[lane] || b_accepted[lane]) begin
m_b_valid[lane]=aw_seen[lane] && w_count[lane]==beats(lane_tx[lane],0) && ((cycles+lane)%5)!=0;
m_b_payload[lane*BW+:BW]=response_b(lane_tx[lane],0);
end
if(!m_r_valid[lane] || r_accepted[lane]) begin
m_r_valid[lane]=read_active[lane] && ((cycles+lane)%7)>1;
m_r_payload[lane*RW+:RW]=response_r(read_tx[lane],read_beat[lane],0);
end
end
end end
'''.replace('SEED',str(seed))
    score='''always @(posedge clk) begin
if(reset) begin
aw_accepted=0;w_accepted=0;ar_accepted=0;b_accepted='0;r_accepted='0;
cycles=0;aw_sent=0;ar_sent=0;w_tx=0;w_beat=0;b_received=0;r_tx=0;r_beat=0;r_words=0;
early_data=0;early_reads=0;overlap=0;error_write_256=0;error_read_256=0;aw_seen='0;read_active='0;aw_stall='0;w_stall='0;ar_stall='0;b_stall=0;r_stall=0;
for(integer lane=0;lane<N;lane++) begin w_count[lane]=0;lane_tx[lane]=0;read_tx[lane]=0;read_beat[lane]=0;next_aw[lane]=lane;next_ar[lane]=(lane-1+N+1)%(N+1);end
end else begin
cycles=cycles+1;
aw_accepted=s_aw_valid && s_aw_ready;w_accepted=s_w_valid && s_w_ready;ar_accepted=s_ar_valid && s_ar_ready;
b_accepted=m_b_valid & m_b_ready;r_accepted=m_r_valid & m_r_ready;
if(s_aw_valid && s_aw_ready) aw_sent=aw_sent+1;
if(s_ar_valid && s_ar_ready) ar_sent=ar_sent+1;
if(s_w_valid && s_w_ready) begin
if(w_beat==beats(w_tx,0)-1) begin w_tx=w_tx+1;w_beat=0;end else w_beat=w_beat+1;
end
if(aw_sent>b_received && ar_sent>r_tx) overlap=1;
if(b_stall && (!s_b_valid || s_b_payload!==held_b)) $fatal(1,"unstable B");
if(r_stall && (!s_r_valid || s_r_payload!==held_r)) $fatal(1,"unstable R");
b_stall=s_b_valid && !s_b_ready;held_b=s_b_payload;
r_stall=s_r_valid && !s_r_ready;held_r=s_r_payload;
if(s_b_valid && (b_received>=aw_sent || b_received>=w_tx)) $fatal(1,"early B before AW/W completion");
if(s_r_valid && r_tx>=ar_sent) $fatal(1,"unsolicited R");
for(integer lane=0;lane<N;lane++) begin
if(aw_stall[lane] && (!m_aw_valid[lane] || m_aw_payload[lane*AW+:AW]!==held_aw[lane])) $fatal(1,"unstable AW");
if(w_stall[lane] && (!m_w_valid[lane] || m_w_payload[lane*WW+:WW]!==held_w[lane])) $fatal(1,"unstable W");
if(ar_stall[lane] && (!m_ar_valid[lane] || m_ar_payload[lane*AW+:AW]!==held_ar[lane])) $fatal(1,"unstable AR");
aw_stall[lane]=m_aw_valid[lane] && !m_aw_ready[lane];held_aw[lane]=m_aw_payload[lane*AW+:AW];
w_stall[lane]=m_w_valid[lane] && !m_w_ready[lane];held_w[lane]=m_w_payload[lane*WW+:WW];
ar_stall[lane]=m_ar_valid[lane] && !m_ar_ready[lane];held_ar[lane]=m_ar_payload[lane*AW+:AW];
if(m_aw_valid[lane] && m_aw_ready[lane]) begin
if(aw_seen[lane] || next_aw[lane]>=aw_sent || m_aw_payload[lane*AW+:AW]!==request(next_aw[lane],0,1)) $fatal(1,"AW route/record/ownership");
lane_tx[lane]=next_aw[lane];next_aw[lane]=next_aw[lane]+N+1;aw_seen[lane]=1;
for(integer k=0;k<w_count[lane];k++) if(queued_w[lane][k]!==word(lane_tx[lane],k,1)) $fatal(1,"early W order/record");
end
if(m_w_valid[lane] && m_w_ready[lane]) begin
if(w_count[lane]>=512) $fatal(1,"excess write data");
if(aw_seen[lane] && (w_count[lane]>=beats(lane_tx[lane],0) || m_w_payload[lane*WW+:WW]!==word(lane_tx[lane],w_count[lane],1))) $fatal(1,"W route/order/record");
if(!aw_seen[lane]) early_data=1;
queued_w[lane][w_count[lane]]=m_w_payload[lane*WW+:WW];w_count[lane]=w_count[lane]+1;
end
if(m_b_valid[lane] && m_b_ready[lane]) begin aw_seen[lane]=0;w_count[lane]=0;end
if(m_ar_valid[lane] && m_ar_ready[lane]) begin
if(read_active[lane] || next_ar[lane]>=ar_sent || m_ar_payload[lane*AW+:AW]!==request(next_ar[lane],1,1)) $fatal(1,"AR route/record/ownership");
read_active[lane]=1;read_tx[lane]=next_ar[lane];next_ar[lane]=next_ar[lane]+N+1;read_beat[lane]=0;
end
if(m_r_valid[lane] && m_r_ready[lane]) begin
if(read_beat[lane]==beats(read_tx[lane],1)-1) read_active[lane]=0;
else read_beat[lane]=read_beat[lane]+1;
end
end
if(s_b_valid && s_b_ready) begin
if(s_b_payload!==response_b(b_received,route(b_received,0)==N)) $fatal(1,"B ownership/ID/error/record");
if(route(b_received,0)==N && beats(b_received,0)==256) error_write_256=1;
b_received=b_received+1;
end
if(s_r_valid && s_r_ready) begin
if(s_r_payload!==response_r(r_tx,r_beat,route(r_tx,1)==N)) $fatal(1,"R ownership/ID/error/length/record tx=%0d beat=%0d got=%h expected=%h ar=%0d",r_tx,r_beat,s_r_payload,response_r(r_tx,r_beat,route(r_tx,1)==N),ar_sent);
r_words=r_words+1;if(b_received==0) early_reads=1;
if(r_beat==beats(r_tx,1)-1) begin
if(route(r_tx,1)==N && beats(r_tx,1)==256) error_read_256=1;
r_tx=r_tx+1;r_beat=0;end else r_beat=r_beat+1;
end
end end
initial begin
repeat(3) @(negedge clk);#1 reset=0;
repeat(35) @(negedge clk);#1;
if(aw_sent==0 || ar_sent==0 || (w_tx==0 && w_beat==0) || b_received!=0 || r_tx!=0) $fatal(1,"queued reset setup");
reset=1;repeat(3) @(negedge clk);#1 epoch=1;reset=0;
wait(b_received==TXNS && r_tx==TXNS);
repeat(20) @(negedge clk);#1;
if(aw_sent!=TXNS || ar_sent!=TXNS || w_tx!=TXNS || !early_data || !early_reads || !overlap) $fatal(1,"missing acceptance/concurrency witnesses");
if(FULL && N==2 && (!error_write_256 || !error_read_256)) $fatal(1,"missing 256-beat decode-error witnesses");
$display("PASS AXI address demux N=%0d FULL=%0d ADDR=%0d writes=%0d reads=%0d words=%0d",N,FULL,A,b_received,r_tx,r_words);$finish;
end
initial begin #2000000;$fatal(1,"address router timeout");end
endmodule
'''
    sv,tb,binary=directory/'dut.sv',directory/'tb.sv',directory/'sim'
    generated=[]
    if native:
        wrapper,design=native_design(directory,count,full,a,seed,native_mode,ports)
        generated=[design]
    sv.write_text(wrapper);tb.write_text(header+drive+score)
    primitives=ROOT/'rtl/pigen_primitives.sv'
    run(['verilator','--lint-only','--top-module','dut',sv,*generated,primitives],directory)
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,*generated,primitives],directory)
    assert 'PASS AXI address demux' in run(['vvp',binary],directory)
    print(f'PASS AXI address routing primitive: outputs={count}, full={full}, address={a}, seed={seed}, native={native_mode if native else False}, legal bursts/errors, early W, independent reads/writes, stalls/reset')


def native_design(directory,count,full,address_width,seed,mode,ports):
    a=address_width;base=1<<(a-1)
    config=f'addr={a},clock=ACLK,resetn=ARESETn'
    if full: config+=',id=5,awuser=6,wuser=6,buser=16,aruser=6,ruser=19'
    protocol=f'uint[{64 if full else 32}] {"axi4" if full else "axi4_lite"}[{config}]'
    controls='input wire ACLK,input wire ARESETn'
    source=''
    if mode=='child':
        transfers='m.aw<=s.aw; m.w<=s.w; s.b<=m.b; m.ar<=s.ar; s.r<=m.r;'
        source=f'module leaf({controls},input {protocol} s,output {protocol} m); always @(posedge ACLK) if(ARESETn) begin {transfers} end endmodule\n'
    standalone=mode=='standalone'
    name='configured_memory_route #(parameter BASE=0,parameter STRIDE=4096,parameter WINDOW_BYTES=256)' if mode=='parameterized' else 'native_memory_route'
    source+=f'{"fabric" if standalone else "module"} {name}({controls},input {protocol} fifo[3] s,output {protocol} skid m[{count}]'+(') begin\n' if standalone else ');\n')
    if mode=='child':
        source+='leaf input_view(.ACLK(ACLK),.ARESETn(ARESETn),.s(s));\n'
        source+=''.join(f'leaf output{i}(.ACLK(ACLK),.ARESETn(ARESETn),.m(m[{i}]));\n' for i in range(count))
    left='input_view.m' if mode=='child' else 's'
    if mode=='generated': source+='for(genvar lane=0;lane<'+str(count)+';lane++) begin : routes fabric begin s -> m[lane] at ['+f"{a}'h{base:x}+lane*65536:{a}'h{base:x}+lane*65536+4095"+']; endfabric end\n'
    else:
        for lane in range(count):
            right=f'output{lane}.s' if mode=='child' else f'm[{lane}]'
            link=f"{left} -> {right} at [{a}'h{base+lane*65536:x}:{a}'h{base+lane*65536+4095:x}];"
            if mode=='parameterized': link=f"s -> {right} at [BASE+{lane}*STRIDE:BASE+{lane}*STRIDE+WINDOW_BYTES-1];"
            source+=link+'\n' if standalone else f'fabric route{lane} begin {link} endfabric\n'
    source+='endfabric\n' if standalone else 'endmodule\n'
    if mode=='parameterized':
        source+=f"module native_memory_route({controls},input {protocol} fifo[3] s,output {protocol} skid m[{count}]); configured_memory_route #(.BASE({a}'h{base:x}),.STRIDE(65536),.WINDOW_BYTES(4096)) routes(.ACLK(ACLK),.ARESETn(ARESETn),.s(s),.m(m)); endmodule\n"
    pigen,design=directory/'native.pigen',directory/'native.sv'
    pigen.write_text(source)
    run([ROOT/'pigen',pigen,'-o',design,'--no-diagram'],directory)
    # Independent standard-pin mapping, never compiler layout metadata.
    addr=[('id',5,0),('prot',3,5),('addr',a,8),('len',8,a+8),('size',3,a+16),('burst',2,a+19),('lock',1,a+21),('cache',4,a+22),('qos',4,a+26),('region',4,a+30),('user',6,a+34)] if full else [('addr',a,0),('prot',3,a)]
    fields={'aw':addr,'ar':addr,'w':[('data',64,0),('strb',8,64),('last',1,72),('user',6,73)] if full else [('data',32,0),('strb',4,32)],
            'b':[('id',5,0),('resp',2,5),('user',16,7)] if full else [('resp',2,0)],
            'r':[('id',5,64),('data',64,0),('resp',2,69),('last',1,71),('user',19,72)] if full else [('data',32,0),('resp',2,32)]}
    connections=['.ACLK(clk)','.ARESETn(!reset)']
    for side,lanes in [('s',1),('m',count)]:
        for lane in range(lanes):
            prefix='s' if side=='s' else f'm_{lane}'
            for channel,entries in fields.items():
                width=sum(bits for _,bits,_ in entries);offset=lane*width if side=='m' else 0
                for field,bits,lsb in entries:
                    connections.append(f'.{prefix}_{channel}{field}({side}_{channel}_payload[{offset+lsb} +: {bits}])')
                for control in ('valid','ready'):
                    connections.append(f'.{prefix}_{channel}{control}({side}_{channel}_{control}'+(f'[{lane}]' if side=='m' else '')+')')
    wrapper='module dut(input logic clk,reset,'+','.join(ports)+');\n'
    wrapper+='native_memory_route instance0('+','.join(connections)+'); endmodule\n'
    return wrapper,design


def source_contracts(directory):
    protocol='uint[32] axi4_lite[addr=16]'
    header=f'module checked(input wire clk,input wire reset,input wire runtime,input {protocol} s,output {protocol} m[2]);'
    cases=[
        ('missing','s -> m[0]; s -> m[1];','windows on every link'),
        ('mixed','s -> m[0] at [0:15]; s -> m[1];','windows on every link'),
        ('late','s -> m[0]; s -> m[1] at [16:31];','windows on every link'),
        ('overlap','s -> m[0] at [0:15]; s -> m[1] at [15:31];','must not overlap'),
        ('duplicate','s -> m[0] at [0:15]; s -> m[0] at [16:31];','distinct destinations'),
        ('direct','s > m[0] at [0:15];','routed native memory-mapped'),
        ('reversed','s -> m[0] at [16:15];','base <= limit'),
        ('negative','s -> m[0] at [-1:15];','nonnegative'),
        ('overflow','s -> m[0] at [0:65536];','address width'),
        ('runtime','s -> m[0] at [runtime:15];',''),
        ('unknown',"s -> m[0] at [0:16'hxxxx];",''),
        ('wide',"s -> m[0] at [0:128'h10000000000000000];",''),
        ('no_colon','s -> m[0] at [15];','base:limit'),
        ('empty_base','s -> m[0] at [:15];','base:limit'),
        ('empty_limit','s -> m[0] at [0:];','base:limit'),
        ('no_bracket','s -> m[0] at 0:15;','[base:limit]'),
    ]
    for name,links,message in cases:
        source=header+'fabric begin '+links+' endfabric endmodule'
        path,output=directory/(name+'.pigen'),directory/(name+'.sv')
        path.write_text(source);output.write_text('preserve output')
        diagnostic=run([ROOT/'pigen',path,'-o',output,'--no-diagram'],directory,expect=False)
        assert message in diagnostic,(name,diagnostic)
        assert output.read_text()=='preserve output',name
    for family in ('axi4_stream','buf'):
        declaration='uint[32] '+family
        path=directory/(family+'.pigen')
        path.write_text(header.replace(protocol,declaration)+'fabric begin s -> m[0] at [0:15]; endfabric endmodule')
        assert 'address windows require' in run([ROOT/'pigen',path,'-o',directory/'bad.sv','--no-diagram'],directory,expect=False)
    path=directory/'expressions.pigen'
    path.write_text(header+'localparam BASE=16; fabric begin s -> m[0] at [1 ? 0 : 2:BASE-1]; s -> m[1] at [BASE:(0 ? 100 : 31)]; endfabric endmodule')
    run([ROOT/'pigen',path,'-o',directory/'expressions.sv','--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','checked',directory/'expressions.sv',ROOT/'rtl/pigen_primitives.sv'],directory)
    path=directory/'parameter_overlap.pigen';output=directory/'parameter_overlap.sv'
    child=header.replace('module checked(', 'module adjustable #(parameter BASE0=0,parameter BASE1=16)(')
    child+='fabric begin s -> m[0] at [BASE0:BASE0+15]; s -> m[1] at [BASE1:BASE1+15]; endfabric endmodule\n'
    path.write_text(child+header+'adjustable #(.BASE1(15)) routes(.clk(clk),.reset(reset),.runtime(runtime),.s(s),.m(m)); endmodule')
    output.write_text('preserve output')
    diagnostic=run([ROOT/'pigen',path,'-o',output,'--no-diagram'],directory,expect=False)
    assert 'must not overlap' in diagnostic,diagnostic
    assert output.read_text()=='preserve output'
    print('PASS native address-window source contracts: scoped expressions, malformed/invalid bounds, ownership, overlap, family and output preservation')


def configuration_negatives(directory):
    for name,bases,limits,message in [('overlap','20','73','overlapping AXI address windows'),('reversed','40','33','reversed AXI address window')]:
        sv,binary=directory/f'{name}.sv',directory/f'{name}.sim'
        sv.write_text("module bad; pigen_axi_demux #(.N(2),.AW_W(4),.AR_W(4),.ADDR_W(4),.BASES(8'h"+bases+"),.LIMITS(8'h"+limits+")) dut(); initial #1 $finish; endmodule\n")
        run(['iverilog','-g2012','-s','bad','-o',binary,sv,ROOT/'rtl/pigen_primitives.sv'],directory)
        assert message in run(['vvp',binary],directory,expect=False)
    print('PASS AXI address routing configuration: overlapping and reversed windows reject')


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-axi-demux-'))
    try:
        for count,full,address_width in ((1,False,32),(2,False,40),(2,True,40),(3,True,64),(5,True,64)):
            for seed in (1,17):
                case(root/f'n{count}_f{int(full)}_a{address_width}_s{seed}',count,full,address_width,seed)
        for mode,count,full,address_width in (
                ('boundary',1,False,32),('boundary',2,True,40),
                ('generated',3,True,64),('child',2,False,40),
                ('child',2,True,40),('standalone',2,False,40),
                ('standalone',3,True,64),('parameterized',2,False,40),
                ('parameterized',3,True,64)):
            case(root/f'native_{mode}_{count}_{full}',count,full,address_width,17,True,mode)
        source_contracts(root)
        configuration_negatives(root)
    except Exception:
        print(f'AXI address routing evidence retained at {root}')
        raise
    else:
        shutil.rmtree(root)


if __name__=='__main__':
    main()
