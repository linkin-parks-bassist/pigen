#!/usr/bin/env python3
"""Pipeline entry obeys earlier procedural exits and retains queued packets."""
from pathlib import Path
import shutil
import sys
import tempfile
from native_protocol_test import ROOT, run, channels
from native_array_test import case


def simple_liveness(root,tool,modes=('continue','break','goto','continue_after','break_after','goto_after')):
    for mode in modes:
        directory=root/mode
        directory.mkdir()
        source,sv,tb,binary=[directory/name for name in ('relay.pigen','relay.sv','tb.sv','sim')]
        packet=('pipeline packet begin bit[8] first;bit[8] second;'
                'stage begin first<=source;end stage begin second<=first+8\'d1;end '
                'yield second;endpipeline destination<=packet;')
        kind=mode.removesuffix('_after')
        after=mode.endswith('_after')
        exit_statement=f'if(pause){"goto idle" if kind=="goto" else kind};'
        statements=packet+exit_statement if after else exit_statement+packet
        if kind=='goto':
            process=('fsm @(posedge clk)begin if(reset)goto run;state run:begin '
                     +statements+'end state idle:goto run;end')
        else:
            process=('always @(posedge clk)if(!reset)foreach(lanes[i])begin '
                     +statements+'end')
        source.write_text('module relay(input wire clk,input wire reset,input logic pause,input wire[0:0] lanes,'
                          'input bit[8] buf source,output bit[8] buf destination);'+process+' endmodule')
        # An explicit two-state control model decides whether the authored
        # invocation reaches the pipeline. Output storage may drain while
        # the worker is paused; only new consumption must be inhibited.
        tb.write_text('''module tb;logic clk=0,reset=1,pause=1;logic[0:0] lanes=0;
logic[7:0] source=0;logic source_valid=0;wire source_ready;wire[7:0] destination;
wire destination_valid;logic destination_ready=0,took=0,held_valid=0;logic[7:0] held;
integer sent=0,received=0,cycles=0,blocked=0,early=0,x_seen=0,z_seen=0;logic idle=0,entered;
always #5 clk=~clk;relay dut(.*);
initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)if(!reset)begin
 if(cycles<12)pause=1;else case(cycles%13)3,4,5:pause=1;7:pause=1'bx;8:pause=1'bz;default:pause=0;endcase
 destination_ready=cycles>=20&&(cycles%11)>3;
 if(!source_valid||took)begin source_valid=sent<97;source=8'(sent*37+11);end
end
always @(posedge clk)if(reset)idle<=0;else begin
 entered='''+('1\'b1' if after else '(pause!==1\'b1)')+('&&!idle' if kind=='goto' else '')+''';
 if(source_valid&&!entered)begin
  if(source_ready!==0)$fatal(1,"pipeline consumed after an earlier procedural exit");blocked++;
 end
 if(pause===1'bx)x_seen++;if(pause===1'bz)z_seen++;
 took=source_valid&&source_ready;if(took)begin sent++;if(pause===1'b1)early++;end
 if(held_valid&&(!destination_valid||destination!==held))$fatal(1,"pipeline output changed while stalled");
 held_valid=destination_valid&&!destination_ready;held=destination;
 if(destination_valid&&destination_ready)begin
  if(received>=sent||destination!==8'(received*37+12))$fatal(1,"pipeline packet order/data mismatch");received++;
 end
 if(idle)idle<=0;else if(pause===1'b1)idle<=1;
 cycles++;
end
initial begin wait(received==97);repeat(6)@(posedge clk);#1;
 if(sent!=97||received!=97||destination_valid||'''+('early==0' if after else 'blocked==0')+'''||x_seen==0||z_seen==0)
 $fatal(1,"pipeline liveness/count witness incomplete");
 $display("PASS pipeline '''+mode+''': 97 packets, exit suppression, X/Z controls, output stalls and resume");$finish;end
initial begin #30000;$fatal(1,"pipeline liveness timeout");end endmodule
''')
        run([tool,source,'-o',sv,'--no-diagram'],directory)
        run(['verilator','--lint-only','--top-module','relay',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
        print(run(['vvp',binary],directory).strip(),flush=True)


def native_liveness(root,tool):
    for family in ('axi4_stream','axi4_lite','axi4'):
        for mode in ('direct','aggregate'):
            for exit_kind in ('continue','break','goto','continue_after','break_after','goto_after'):
                directory=root/f'{family}_{mode}_{exit_kind}'
                directory.mkdir()
                case(directory,family,mode,native_alias=True,
                     clocked_foreach='pipeline_'+exit_kind,compiler=tool)
    print('PASS 36 native pipeline exit profiles: prior/later effects, ingress capacities and child queues',flush=True)


def reset_isolation(root,tool):
    for family in ('axi4_stream','axi4_lite','axi4'):
        fields=channels(family,32)
        # Use complete standard-pin layouts matching the common queue oracle.
        options={'axi4_stream':'keep=1,strb=1,last=1,id=4,dest=3,user=7','axi4_lite':'addr=40',
                 'axi4':'addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13'}[family]
        width=2*len(fields);full=(1<<width)-1;lane_mask=(1<<len(fields))-1
        for placement in ('before','after'):
            directory=root/f'reset_{family}_{placement}'
            directory.mkdir()
            source,sv,tb,binary=[directory/name for name in ('relay.pigen','relay.sv','tb.sv','sim')]
            types=[];body=[];declarations=[];connections=['.clk(clk)','.reset(reset)','.clear(clear)',
                                                      '.deliver(deliver)','.seen(seen)']
            drive=[];valids=[];readies=[];out_valids=[]
            for index,(channel,layout) in enumerate(fields.items()):
                member='beat' if channel=='t' else channel
                incoming,outgoing=('m','s') if channel in ('b','r') else ('s','m')
                typename=f'record_{channel}';packet=f'packet_{channel}'
                types.append('typedef struct packed {'+''.join(f'bit[{bits-1}:0] {field};' for field,bits in reversed(layout))+'} '+typename+';')
                declaration=(f'if(!clear[lane])begin pipeline {packet} begin {typename} first;{typename} second;'
                             f'stage begin first<={typename}\'({incoming}[lane].{member});end '
                             'stage begin second<=first;end yield second;endpipeline end ')
                action=f'if(clear[lane])pipeline_reset({packet});'
                body.append(action+declaration if placement=='before' else declaration+action)
                body.append(f'if(deliver){outgoing}[lane].{member}<=uint[{sum(bits for _,bits in layout)}]\'({packet});'
                            f'seen[uint[{(width-1).bit_length()}]\'(lane*{len(fields)}+{index})]<=valid({packet});')
                for lane in (0,1):
                    for endpoint in ('s','m'):
                        for field,bits in layout+[('valid',1),('ready',1)]:
                            pin=f'{endpoint}_{lane}_{channel}{field}'
                            input_pin=(endpoint==incoming)!=(field=='ready')
                            declarations.append(f'{"logic" if input_pin else "wire"}[{bits-1}:0] {pin}'+('=0;' if input_pin else ';'))
                            connections.append(f'.{pin}({pin})')
                            if endpoint==incoming and field not in ('valid','ready'):
                                drive.append(f'{pin}={bits}\'(trial*37+{lane}*11+{index}+1);')
                    valids.append(f'{incoming}_{lane}_{channel}valid')
                    readies.append(f'{incoming}_{lane}_{channel}ready')
                    out_valids.append(f'{outgoing}_{lane}_{channel}valid')
            source.write_text('\n'.join(types)+f'\nmodule relay(input wire clk,input wire reset,input wire[1:0] clear,'
                              f'input wire deliver,input uint[32] {family}[{options}] buf s[0:1],'
                              f'output uint[32] {family}[{options}] buf m[0:1],output logic[{width-1}:0] seen);'
                              'always @(posedge clk)foreach(s[lane])begin '+''.join(body)+'end endmodule')
            tb.write_text('module tb;logic clk=0,reset=1,deliver=0;logic[1:0] clear=0;'
                          f'wire[{width-1}:0] seen;logic[{width-1}:0] expected;integer trial,waited;always #5 clk=~clk;'+
                          ''.join(declarations)+'relay dut('+','.join(connections)+');initial begin '
                          'for(trial=0;trial<32;trial++)begin @(negedge clk);reset=1;clear=0;'
                          'repeat(3)@(negedge clk);reset=0;'+''.join(drive)+''.join(f'{v}=1;' for v in valids)+
                          '@(posedge clk);if(!('+ '&&'.join(readies)+'))$fatal(1,"input capture unavailable");'
                          '@(negedge clk);'+''.join(f'{v}=0;' for v in valids)+
                          f'waited=0;while(seen!=={width}\'d{full}&&waited<30)begin @(negedge clk);waited++;end '
                          f'if(seen!=={width}\'d{full})$fatal(1,"pipeline packets were not parked");'
                          f'clear=2\'(1<<(trial&1));expected={width}\'d{full}^({width}\'d{lane_mask}<<((trial&1)*{len(fields)}));'
                          'repeat(3)@(negedge clk);clear=0;repeat(2)@(negedge clk);'
                          'if(seen!==expected)$fatal(1,"pipeline_reset crossed lexical lanes: actual=%h expected=%h",seen,expected);'
                          'clear=3;repeat(3)@(negedge clk);'
                          'if(seen!==0||'+ '||'.join(out_valids)+')$fatal(1,"pipeline reset/observation leaked packets");end '
                          f'$display("PASS {family} {placement}: 32 selective reset epochs, both lexical lanes and every channel");$finish;end '
                          'initial begin #100000;$fatal(1,"pipeline reset timeout");end endmodule')
            run([tool,source,'-o',sv,'--no-diagram'],directory)
            run(['verilator','--lint-only','--top-module','relay',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
            run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
            print(run(['vvp',binary],directory).strip(),flush=True)


def main():
    tool=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'pigen'
    root=Path(tempfile.mkdtemp(prefix='pigen-pipeline-liveness-'))
    print(root,flush=True)
    try:
        simple_liveness(root,tool)
        reset_isolation(root,tool)
        native_liveness(root,tool)
    except BaseException:
        print(f'Pipeline liveness evidence retained in {root}',flush=True)
        raise
    else:
        shutil.rmtree(root)


if __name__=='__main__':
    main()
