#!/usr/bin/env python3
"""Competing independent masters against actual native register/bridge cores."""
from pathlib import Path
import re
from native_axi_core_fabric_test import run, ROOT


def case(full, seed, switched=False):
    count=2 if switched and not full else 3
    family = 'axi4' if full else 'axi4_lite'
    directory = ROOT / 'build/native-core-contention' / (f'{family}-{seed}'+('-switched' if switched else ''))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'commands.log').write_text('')
    original = (ROOT / ('cores/axi4/bridge.pigen' if full else 'cores/axil/registers.pigen')).read_text()
    core_name = 'pigen_axi4_bridge' if full else 'pigen_axil_registers'
    source = original.replace('module ' + core_name, 'module real_target', 1)
    protocol = family + ('[addr=32,id=2]' if full else '[addr=32]')
    ports = ['input wire clk', 'input wire reset'] + [f'input uint[32] {protocol} s{i}' for i in range(count)]
    backend = ['output bit[68] buf write_request', 'input bit[2] buf write_reply',
               'output bit[32] buf read_request', 'input bit[34] buf read_reply'] if full else []
    source += 'module contended_core(' + ','.join(ports + backend) + ');\n'
    bindings = ''.join(f',.{p}({p})' for p in ('write_request', 'write_reply', 'read_request', 'read_reply')) if full else ''
    source += f'real_target target(.clk(clk),.reset(reset){bindings});\n'
    source += 'fabric requests begin\n' + ''.join(f's{i} -> target.s_axi;\n' for i in range(count)) + 'endfabric\nendmodule\n'
    if switched:
        # Keep the actual core untouched; the SV bench connects every target pin.
        target_protocol=family+('[addr=32,id=4]' if full else '[addr=32]')
        source=original.replace('module '+core_name,'module real_target',1)
        source+=f'fabric memory_switch(input wire clk,input wire reset,input uint[32] {protocol} s[{count}],output uint[32] {target_protocol} m[2]) begin\n'
        for i in range(count):
            for target in range(2):
                base=target*(4096 if full else 8)
                limit=base+(4095 if full else 7)
                source+=f's[{i}] -> m[{target}] at [{base}:{limit}];\n'
        source+='endfabric\nmodule contended_core('+','.join(ports+[f'output uint[32] {target_protocol} m[2]'])+');\n'
        source+="memory_switch routes(.clk(clk),.reset(reset),.s('{"+','.join(f's{i}' for i in range(count))+"}));\n"
        step=4096 if full else 8
        source+=f'for(genvar target=0;target<2;target++) begin : outputs fabric begin routes.m[target] -> m[target] at [target*{step}:(target+1)*{step}-1]; endfabric end\nendmodule\n'

    pigen, sv, tb, sim = [directory / p for p in ('core.pigen', 'core.sv', 'tb.sv', 'sim')]
    pigen.write_text(source)
    run([ROOT / 'pigen', pigen, '-o', sv, '--no-diagram'], directory)
    run(['verilator', '--lint-only', '--top-module', 'contended_core', sv, ROOT / 'rtl/pigen_primitives.sv'], directory)
    fields = {'aw': [('addr',32),('prot',3)], 'w':[('data',32),('strb',4)],
              'b':[('resp',2)], 'ar':[('addr',32),('prot',3)], 'r':[('data',32),('resp',2)]}
    if full:
        for ch in ('aw','ar'):
            fields[ch] += [('id',2),('len',8),('size',3),('burst',2),('lock',1),('cache',4),('qos',4),('region',4)]
        fields['w'] += [('last',1)];fields['b'] += [('id',2)];fields['r'] += [('id',2),('last',1)]
    declarations, connects, drives = [], ['.clk(clk)', '.reset(reset)'], []
    for ch, members in fields.items():
        for field, width in members + [('valid',1),('ready',1)]:
            name = ch + field
            declarations.append(f'logic [{width-1}:0] {name}[0:N-1];')
            connects += [f'.s{i}_{name}({name}[{i}])' for i in range(count)]
            if (ch in ('aw','w','ar') and field=='ready') or (ch in ('b','r') and field!='ready'):
                continue
            if field=='valid':
                expression = 'phase[i]==0&&!aw_sent[i]&&(awvalid[i]||rng[1:0]!=0)' if ch=='aw' else 'phase[i]==0&&w_sent[i]<beats(seq[i])&&(wvalid[i]||rng[4:2]!=0)' if ch=='w' else 'phase[i]==2&&(arvalid[i]||rng[7:5]!=0)'
            elif field=='ready': expression = f'phase[i]=={1 if ch=="b" else 3}&&rng[{10 if ch=="b" else 13}:{8 if ch=="b" else 11}]!=0&&!(cycle>=200&&cycle<=242)'
            elif field=='addr':
                expression = '(bad(seq[i]) ? 256 : i*4)' if not full else 'i*256+word_index(seq[i],0)*4'
                if switched: expression='miss(seq[i]) ? 32768 : '+('i*256+word_index(seq[i],0)*4+target_of(i,seq[i])*4096' if full else '(bad(seq[i]) ? 3 : i*4)+target_of(i,seq[i])*8')
            elif field=='data': expression = 'payload(i,seq[i],w_sent[i])'
            elif field=='strb': expression = '(seq[i]+w_sent[i])%16'
            elif field=='last': expression = 'w_sent[i]==beats(seq[i])-1'
            elif field=='id': expression = 'seq[i]%4'
            elif field=='len': expression = 'beats(seq[i])-1'
            elif field=='size': expression = '2'
            elif field=='burst': expression = 'bad(seq[i]) ? 3 : seq[i]%3'
            else: expression = '0'
            if switched and field=='valid' and ch=='aw':
                expression='phase[i]==0&&!aw_sent[i]&&(seq[i]%3!=1||w_sent[i]>0)&&(awvalid[i]||seq[i]%3==2||rng[1:0]!=0)'
            if switched and field=='valid' and ch=='w':
                expression='phase[i]==0&&w_sent[i]<beats(seq[i])&&(seq[i]%3!=0||aw_sent[i])&&(wvalid[i]||seq[i]%3==2||rng[4:2]!=0)'
            drives.append(f'{name}[i]=reset ? 0 : {width}\'({expression});')
    if full:
        declarations += ['wire [67:0] write_request;wire write_request_valid;reg write_request_ready=0;',
                         'reg [1:0] write_reply=0;reg write_reply_valid=0;wire write_reply_ready;',
                         'wire [31:0] read_request;wire read_request_valid;reg read_request_ready=0;',
                         'reg [33:0] read_reply=0;reg read_reply_valid=0;wire read_reply_ready;']
        connects += ['.'+p+'('+p+')' for p in ('write_request','write_request_valid','write_request_ready','write_reply','write_reply_valid','write_reply_ready','read_request','read_request_valid','read_request_ready','read_reply','read_reply_valid','read_reply_ready')]
    tb.write_text('''module tb;
localparam N=COUNT;
''' + f'localparam FULL={int(full)};\n' + '\n'.join(declarations) + '''
reg clk=0,reset=1;always #5 clk=~clk;
integer cycle=0,checks=0,epochs=0,aw_first=0,w_first=0,together=0;
integer seq[0:N-1],phase[0:N-1],w_sent[0:N-1],r_got[0:N-1];
reg aw_sent[0:N-1];
reg [31:0] golden[0:N-1][0:63];
reg [31:0] rng=''' + str(seed) + ''';
integer i,j,k,idx,done_count,pending_count;
reg b_stalled[0:N-1],r_stalled[0:N-1];reg [3:0] held_b[0:N-1];reg [36:0] held_r[0:N-1];
function integer beats(input integer q);beats=FULL ? (q%2 ? 4 : 8) : 1;endfunction
function integer bad(input integer q);bad=q%17==16;endfunction
function integer word_index(input integer q,input integer beat);
 if(!FULL||q%3==0)word_index=0;
 else if(q%3==1)word_index=beat;
 else word_index=(q%beats(q)+beat)%beats(q);
endfunction
function [31:0] payload(input integer master,input integer q,input integer beat);
 payload=32'h175329ab+master*32'h11990011+q*32'h10804081+beat*32'h98101005;
endfunction
contended_core dut(''' + ','.join(connects) + ''');
''' + (BACKEND if full else '') + '''
always @(negedge clk) begin
 reset=cycle<3||cycle==241||cycle==242;
 if(cycle==241)begin
  pending_count=0;
  for(integer n=0;n<N;n=n+1)if(aw_sent[n]||w_sent[n]!=0||phase[n]==1||phase[n]==3)pending_count=pending_count+1;
  if(pending_count==0)$fatal(1,"reset witness drained");
  epochs=epochs+1;
 end
 for(integer i=0;i<N;i=i+1)begin
  rng=rng*1664525+1013904223;
''' + '\n'.join(drives) + '''
 end
end
always @(posedge clk) begin
 cycle=cycle+1;
 if(reset)begin
  aw_first=0;w_first=0;together=0;
  for(i=0;i<N;i=i+1)begin
   seq[i]=0;phase[i]=0;aw_sent[i]=0;w_sent[i]=0;r_got[i]=0;b_stalled[i]=0;r_stalled[i]=0;
   for(j=0;j<64;j=j+1)golden[i][j]=0;
  end
 end else begin
  done_count=0;
  for(i=0;i<N;i=i+1)begin
   if(b_stalled[i]&&(!bvalid[i]||4'(B_VALUE)!==held_b[i]))$fatal(1,"B changed under stall");
   if(r_stalled[i]&&(!rvalid[i]||37'(R_VALUE)!==held_r[i]))$fatal(1,"R changed under stall");
   b_stalled[i]=bvalid[i]&&!bready[i];r_stalled[i]=rvalid[i]&&!rready[i];
   held_b[i]=4'(B_VALUE);held_r[i]=37'(R_VALUE);
   if(!aw_sent[i]&&w_sent[i]==0)begin
    if(awvalid[i]&&awready[i]&&wvalid[i]&&wready[i])together=together+1;
    else if(awvalid[i]&&awready[i])aw_first=aw_first+1;
    else if(wvalid[i]&&wready[i])w_first=w_first+1;
   end
   if(awvalid[i]&&awready[i])begin if(phase[i]!=0||aw_sent[i])$fatal(1,"duplicate AW");aw_sent[i]=1;end
   if(wvalid[i]&&wready[i])begin
    if(phase[i]!=0||w_sent[i]>=beats(seq[i]))$fatal(1,"duplicate W");
    idx=word_index(seq[i],w_sent[i]);
    if(!bad(seq[i]))for(k=0;k<4;k=k+1)if(wstrb[i][k])golden[i][idx][k*8+:8]=wdata[i][k*8+:8];
    w_sent[i]=w_sent[i]+1;
   end
   if(phase[i]==0&&aw_sent[i]&&w_sent[i]==beats(seq[i]))phase[i]=1;
   if(bvalid[i]&&bready[i])begin
    if(phase[i]!=1||bresp[i]!==2'(bad(seq[i]) ? 3 : 0))$fatal(1,"B route/status master %0d",i);
''' + ('    if(bid[i]!==2\'(seq[i]%4))$fatal(1,"B ID routing");\n    if((backend_w_seq[i]==seq[i] ? backend_w_count[i] : 0)!=write_count(seq[i]))$fatal(1,"backend write count");\n' if full else '') + '''
    phase[i]=2;checks=checks+1;
   end
   if(arvalid[i]&&arready[i])begin if(phase[i]!=2)$fatal(1,"duplicate AR");phase[i]=3;r_got[i]=0;end
   if(rvalid[i]&&rready[i])begin
    idx=word_index(seq[i],r_got[i]);
    if(phase[i]!=3||rresp[i]!==2'(bad(seq[i]) ? 3 : 0)||rdata[i] !== (bad(seq[i]) ? 32'd0 : golden[i][idx]))$fatal(1,"R master %0d sequence %0d beat %0d got %h expected %h status %d",i,seq[i],r_got[i],rdata[i],golden[i][idx],rresp[i]);
''' + ('    if(rid[i]!==2\'(seq[i]%4)||rlast[i] !== (r_got[i]==beats(seq[i])-1))$fatal(1,"R ID/last routing");\n    if(r_got[i]==beats(seq[i])-1&&(backend_r_seq[i]==seq[i] ? backend_r_count[i] : 0)!=(bad(seq[i]) ? 0 : beats(seq[i])))$fatal(1,"backend read count");\n' if full else '') + '''
    r_got[i]=r_got[i]+1;checks=checks+1;
    if(r_got[i]==beats(seq[i]))begin seq[i]=seq[i]+1;phase[i]=seq[i]==32 ? 4 : 0;aw_sent[i]=0;w_sent[i]=0;end
   end
   if(phase[i]==4)done_count=done_count+1;
  end
  if(epochs==1&&done_count==N)begin
   if(aw_first==0||w_first==0||together==0)$fatal(1,"missing AW/W arrival-order coverage %0d/%0d/%0d",aw_first,w_first,together);
   $display("PASS contended real core %0d checks, AW-first=%0d W-first=%0d simultaneous=%0d",checks,aw_first,w_first,together);$finish;
  end
 end
 if(cycle>60000)$fatal(1,"contended core timeout");
end
''' + '\nendmodule\n')
    text = tb.read_text().replace('COUNT',str(count)).replace('B_VALUE', '{bid[i],bresp[i]}' if full else 'bresp[i]').replace('R_VALUE', '{rid[i],rlast[i],rresp[i],rdata[i]}' if full else '{rresp[i],rdata[i]}')
    if switched: text=switched_bench(text,fields,full,count)
    tb.write_text(text)
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    assert 'PASS contended real core' in run(['vvp',sim],directory)
    print(f'PASS real {family} core, {count} competing masters switched={switched} seed={seed}: IDs, masked memory, bursts/errors, stalls and queued reset{"; independent switched targets" if switched else ""}')


def switched_bench(text,fields,full,count):
    # Reuse the independent master/golden oracle with two physical core targets.
    # Native field layouts and generated internal names are never inspected.
    text=text.replace('function integer bad(input integer q);', 'function integer target_of(input integer master,q);target_of=(master+q)%2;endfunction\nfunction integer miss(input integer q);miss=q%11==10;endfunction\nfunction integer bad(input integer q);')
    text=text.replace('!bad(seq[i])','!bad(seq[i])&&!miss(seq[i])')
    text=text.replace('bad(seq[i]) ? 3 : 0','bad(seq[i])||miss(seq[i]) ? 3 : 0')
    text=text.replace('bad(seq[i]) ? 32\'d0', '(bad(seq[i])||miss(seq[i])) ? 32\'d0')
    if not full: text=text.replace('if(!FULL||q%3==0)word_index=0;', 'if(!FULL)word_index=q%2; else if(q%3==0)word_index=0;')
    else:
        text=text.replace('idx=word_index(seq[i],w_sent[i]);','idx=word_index(seq[i],w_sent[i])+(seq[i]%2)*32;')
        text=text.replace('idx=word_index(seq[i],r_got[i]);','idx=word_index(seq[i],r_got[i])+(seq[i]%2)*32;')
    # Each slave remains an unchanged real core with an independent standard-pin
    # interface. Full target IDs include the source provenance prefix.
    declarations=['wire target0_enabled=cycle>=800;'];instances=[];router=[]
    for target in range(2):
        connections=['.clk(clk)','.reset(reset)']
        for channel,members in fields.items():
            for field,width in members+[('valid',1),('ready',1)]:
                if field=='id': width=4
                pin=f't{target}_{channel}{field}'
                declarations.append(f'wire [{width-1}:0] {pin};')
                router.append(f'.m_{target}_{channel}{field}({pin})')
                if target==0 and channel in ('aw','w','ar') and field=='valid':
                    connections.append(f'.s_axi_{channel}{field}({pin}&&target0_enabled)')
                elif target==0 and channel in ('aw','w','ar') and field=='ready':
                    declarations.append(f'wire core0_{channel}ready; assign {pin}=core0_{channel}ready&&target0_enabled;')
                    connections.append(f'.s_axi_{channel}{field}(core0_{channel}ready)')
                else: connections.append(f'.s_axi_{channel}{field}({pin})')
        if full:
            connections += [f'.{port}(t{target}_{port})' for port in BACKEND_PORTS]
        instances.append('real_target target'+str(target)+'('+','.join(connections)+');')
    text=text.replace('contended_core dut(', '\n'.join(declarations)+'\n'+ 'contended_core dut(')
    if full:
        for port in BACKEND_PORTS:
            text=text.replace(f',.{port}({port})','')
    end=text.index(');',text.index('contended_core dut('))
    text=text[:end]+','+','.join(router)+text[end:]
    end=text.index(');',text.index('contended_core dut('))+2
    text=text[:end]+'\n'+'\n'.join(instances)+text[end:]
    if full:
        # Replace the external backend declarations/model with two independent
        # agents. All counters remain in the oracle, outside the generated RTL.
        start=text.index('wire [67:0] write_request;')
        end=text.index('\nreg clk=',start)
        old_declarations=text[start:end]
        text=text[:start]+''.join(rename_backend(old_declarations,t) for t in range(2))+text[end:]
        start=text.index('reg [31:0] memory[0:191];')
        end=text.index('always @(negedge clk) begin\n reset=',start)
        backend=text[start:end]
        backend=backend.replace('!bad(q)', '!bad(q)&&!miss(q)')
        backend=backend.replace('bad(seq[master])', '(bad(seq[master])||miss(seq[master]))')
        backend=backend.replace('write_request[63:32]>=768', '(write_request[63:32]&4095)>=768')
        backend=backend.replace('read_request>=768','(read_request&4095)>=768')
        backend=backend.replace('wi=write_request[63:32]/4;master=write_request[63:32]/256;', 'wi=(write_request[63:32]&4095)/4;master=(write_request[63:32]&4095)/256;')
        backend=backend.replace('ri=read_request/4;master=read_request/256;', 'ri=(read_request&4095)/4;master=(read_request&4095)/256;')
        backend=backend.replace('master*256+word_index(seq[master],beat_cursor)*4','master*256+word_index(seq[master],beat_cursor)*4+target_of(master,seq[master])*4096')
        backend=backend.replace('master*256+word_index(seq[master],backend_r_count[master])*4','master*256+word_index(seq[master],backend_r_count[master])*4+target_of(master,seq[master])*4096')
        text=text[:start]+''.join('if(1) begin : backend'+str(t)+'\n'+rename_backend(backend,t)+'end\n' for t in range(2))+text[end:]
        for name in ('backend_w_seq','backend_w_count','backend_r_seq','backend_r_count'):
            text=text.replace(name+'[i]',f'(target_of(i,seq[i])==0 ? backend0.{name}[i] : backend1.{name}[i])')
        text=text.replace('write_count(seq[i])','backend0.write_count(seq[i])')
        text=text.replace('(bad(seq[i]) ? 0 : beats(seq[i]))','((bad(seq[i])||miss(seq[i])) ? 0 : beats(seq[i]))')
    # Count requests at each physical slave; decode misses must never escape.
    monitor=['integer target_aw[2],target_ar[2];integer independent_b=0,independent_r=0;', 'always @(posedge clk) if(reset) begin for(integer t=0;t<2;t++) begin target_aw[t]=0;target_ar[t]=0;end independent_b=0;independent_r=0;end else begin']
    for t in range(2):
        for ch in ('aw','ar'):
            address=f't{t}_{ch}addr'
            monitor.append(f'if(t{t}_{ch}valid&&t{t}_{ch}ready) begin if({address}>=32768 || {address}[{12 if full else 3}]!==1\'b{t}) $fatal(1,"wrong physical target");target_{ch}[{t}]=target_{ch}[{t}]+1;end')
            if full:
                # Input assignment-pattern order maps master 0 to s[N-1].
                monitor.append(f'if(t{t}_{ch}valid&&t{t}_{ch}ready) begin if(t{t}_{ch}id!==4\'((N-1-({address}/256)%16)*4+seq[({address}/256)%16]%4)) $fatal(1,"target source-ID tag");end')
    monitor.append('if(epochs==1&&!target0_enabled) begin if(bvalid[1]&&bready[1])independent_b=independent_b+1;if(rvalid[1]&&rready[1])independent_r=independent_r+1;end end')
    text=text.replace('always @(negedge clk) begin\n reset=', '\n'.join(monitor)+'\nalways @(negedge clk) begin\n reset=')
    text=text.replace('if(epochs==1&&done_count==N)begin', 'if(epochs==1&&done_count==N)begin\n if(independent_b==0||independent_r==0)$fatal(1,"no independent target progress");\n for(integer t=0;t<2;t++) if(target_aw[t]!=N*15 || target_ar[t]!=N*15) $fatal(1,"missing real-target traffic %0d %0d/%0d",t,target_aw[t],target_ar[t]);')
    return text


BACKEND_PORTS=('write_request','write_request_valid','write_request_ready','write_reply','write_reply_valid','write_reply_ready','read_request','read_request_valid','read_request_ready','read_reply','read_reply_valid','read_reply_ready')


def rename_backend(text,target):
    for port in sorted(BACKEND_PORTS,key=len,reverse=True):
        text=re.sub(r'\b'+port+r'\b',f't{target}_'+port,text)
    return text


BACKEND = r'''
reg [31:0] memory[0:191];
integer wd=-1,rd=-1,wi,ri,lane,master,beat_cursor;
integer backend_w_seq[0:N-1],backend_w_count[0:N-1],backend_w_cursor[0:N-1];
integer backend_r_seq[0:N-1],backend_r_count[0:N-1];
function integer write_count(input integer q);
 write_count=0;
 if(!bad(q))for(integer b=0;b<beats(q);b=b+1)if((q+b)%16!=0)write_count=write_count+1;
endfunction
reg [31:0] sampled_read;
always @(negedge clk)begin
 write_request_ready=!reset&&wd<0&&!write_reply_valid&&rng[17:15]!=0;
 read_request_ready=!reset&&rd<0&&!read_reply_valid&&rng[20:18]!=0;
end
always @(posedge clk)begin
 if(reset)begin
  wd=-1;rd=-1;write_reply_valid=0;read_reply_valid=0;
  for(integer q=0;q<192;q=q+1)memory[q]=0;
  for(integer n=0;n<N;n=n+1)begin backend_w_seq[n]=-1;backend_r_seq[n]=-1;backend_w_count[n]=0;backend_r_count[n]=0;backend_w_cursor[n]=0;end
 end else begin
  if(write_reply_valid&&write_reply_ready)write_reply_valid<=0;
  if(read_reply_valid&&read_reply_ready)read_reply_valid<=0;
  if(wd==0)begin write_reply<=0;write_reply_valid<=1;wd=-1;end
  else if(wd>0)wd=wd-1;
  if(rd==0)begin read_reply<={2'b00,sampled_read};read_reply_valid<=1;rd=-1;end
  else if(rd>0)rd=rd-1;
  if(write_request_valid&&write_request_ready)begin
   if(write_request[33:32]!=0||write_request[63:32]>=768)$fatal(1,"bad backend write address");
   wi=write_request[63:32]/4;master=write_request[63:32]/256;
   if(bad(seq[master]))$fatal(1,"invalid write escaped to backend");
   if(backend_w_seq[master]!=seq[master])begin backend_w_seq[master]=seq[master];backend_w_count[master]=0;backend_w_cursor[master]=0;end
   beat_cursor=backend_w_cursor[master];
   while(beat_cursor<beats(seq[master])&&(seq[master]+beat_cursor)%16==0)beat_cursor=beat_cursor+1;
   if(beat_cursor>=beats(seq[master])||write_request[63:32]!==32'(master*256+word_index(seq[master],beat_cursor)*4)||write_request[67:64]!==4'((seq[master]+beat_cursor)%16)||write_request[31:0]!==payload(master,seq[master],beat_cursor))$fatal(1,"backend write order/address/data/strobes");
   backend_w_cursor[master]=beat_cursor+1;backend_w_count[master]=backend_w_count[master]+1;
   for(lane=0;lane<4;lane=lane+1)if(write_request[64+lane])memory[wi][lane*8+:8]=write_request[lane*8+:8];
   wd=1+(rng%4);
  end
  if(read_request_valid&&read_request_ready)begin
   if(read_request[1:0]!=0||read_request>=768)$fatal(1,"bad backend read address");
   ri=read_request/4;master=read_request/256;
   if(bad(seq[master])||phase[master]!=3)$fatal(1,"unexpected backend read");
   if(backend_r_seq[master]!=seq[master])begin backend_r_seq[master]=seq[master];backend_r_count[master]=0;end
   if(backend_r_count[master]>=beats(seq[master])||read_request!==32'(master*256+word_index(seq[master],backend_r_count[master])*4))$fatal(1,"backend read order/address");
   backend_r_count[master]=backend_r_count[master]+1;
   sampled_read=memory[ri];rd=1+((rng>>4)%4);
  end
 end
end
'''

if __name__ == '__main__':
    for full in (False,True):
        for seed in (1,17,4919):
            case(full,seed)
            case(full,seed,switched=True)
