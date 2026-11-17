#!/usr/bin/env python3
"""Independent packet/latency witnesses for fixed-namespace elastic pipelines."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


PROFILES = {
    'same_name': ('bit[16] x;', ['x<=source;', "x<=x+16'd1;", "x<=x*16'd3;"], 'x', 16, "16'((input_value(index)+1)*3)", False),
    'last_write': ('bit[16] x;', ['x<=source;', "x<=x+16'd1;x<=x+16'd2;"], 'x', 16, "16'(input_value(index)+2)", False),
    'overwrite': ('bit[16] x;', ['x<=source;', "x<=16'd99;"], 'x', 16, "16'd99", False),
    'signed': ('int[8] x;', ["x<=int[8]'(source);", "x<=x>>>1;"], 'x', 8, "8'($signed(8'(input_value(index)))>>>1)", False),
    'wide': ('bit[129] x;', ["x<={113'd0,source};", "x<=(x<<65)|x;"], 'x', 129, "(129'(input_value(index))<<65)|129'(input_value(index))", False),
    'record': ('struct packed {bit[8] hi;bit[8] lo;} x;', ["x<='{hi:source[15:8],lo:source[7:0]};", "x<='{hi:x.lo,lo:x.hi};"], 'x', 16, "{8'(input_value(index)),8'(input_value(index)>>8)}", False),
    'locals': ('bit[16] x;', ["bit[16] combined=source+extra;x<=combined;", "bit[16] wire twice=x*16'd2;bit[16] bumped=twice+16'd1;x<=bumped;"], 'x', 16, "16'((input_value(index)+extra_value(index))*2+1)", True),
    'metadata': ('bit[16] x;', ["x<=source+16'($bits(x));", 'x<=x;'], 'x', 16, "16'(input_value(index)+16)", False),
    'yield_input': ('bit[16] x;', ['x<=source;', "x<=x+16'd1;"], 'x+extra', 16, "16'(input_value(index)+extra_value(index)+1)", True),
    'discarded_read': ('bit[16] x;', ['x<=source;', "x<=extra;x<=16'd99;"], 'x', 16, "16'd99", True),
    'snapshot': ('bit[16] x;bit[16] y;', ["x<=source;y<=source*16'd2;", "x<=x+16'd1;y<=x+16'd5;"], '{x,y}', 32,
                 "{16'(input_value(index)+1),16'(input_value(index)+5)}", False),
    'carry': ('bit[16] x;bit[16] y;', ["x<=source;y<=source+16'd7;", 'x<=x+y;', "y<=x^16'ha55a;"], '{x,y}', 32,
              "{16'(2*input_value(index)+7),16'((2*input_value(index)+7)^16'ha55a)}", False),
    'join': ('bit[16] x;bit[16] y;', ['x<=source;y<=extra;', 'x<=x+y;'], 'x', 16,
             "16'(input_value(index)+extra_value(index))", True),
    'late_input': ('bit[16] x;', ['x<=source;', 'x<=x+extra;', "x<=x*16'd3;"], 'x', 16,
                   "16'((input_value(index)+extra_value(index))*3)", True),
}


def packet_case(root, name, profile):
    fields, stages, result, width, expected, extra = profile
    directory=root/name;directory.mkdir()
    source,sv,tb,sim=[directory/n for n in ('packet.pigen','packet.sv','tb.sv','sim')]
    ports='input wire clk,input wire reset,input logic enable,input bit[16] source,'
    if extra:ports+='input bit[16] extra,'
    source.write_text('module packet_test('+ports+f'output bit[{width}] buf destination);'
                      'always @(posedge clk) if(enable) begin pipeline packet begin '+fields+
                      ''.join('stage begin '+s+'end ' for s in stages)+'yield '+result+
                      ';endpipeline destination<=packet;end endmodule')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','packet_test',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    extra_ports='logic[15:0]extra=0;logic extra_valid=0;wire extra_ready;logic extra_took=0;int extra_sent=0;' if extra else ''
    extra_drive='if(!extra_valid||extra_took)begin extra_valid=extra_sent<257&&(epoch==0||cycle%5>1);extra=extra_value(extra_sent);end' if extra else ''
    extra_count='extra_took=extra_valid&&extra_ready;if(extra_took)extra_sent++;' if extra else ''
    extra_reset='extra_sent=0;extra_valid=0;extra_took=0;' if extra else ''
    extra_checks='if(extra_sent!=257)$fatal(1,"external join consumption count");' if extra else ''
    tb.write_text('''module tb;
logic clk=0,reset=1,enable=1;logic[15:0]source=0;logic source_valid=0;wire source_ready;
wire['''+str(width-1)+''':0]destination;wire destination_valid;logic destination_ready=0;
logic took=0,held_valid=0;logic['''+str(width-1)+''':0]held;int sent=0,received=0,cycle=0,epoch=0,blocked=0;
'''+extra_ports+'''
function automatic logic[15:0] input_value(input int index);return 16'(index*37+11);endfunction
function automatic logic[15:0] extra_value(input int index);return 16'(index*13+7);endfunction
function automatic logic['''+str(width-1)+''':0] expected(input int index);return '''+expected+''';endfunction
packet_test dut(.*);always #5 clk=~clk;
initial begin
 repeat(3)@(negedge clk);reset=0;repeat(80)@(negedge clk);
 if(sent==0||received!=0||!destination_valid)$fatal(1,"queued reset stimulus incomplete");
 reset=1;source_valid=0;'''+('extra_valid=0;' if extra else '')+'''
 repeat(3)@(negedge clk);epoch=1;reset=0;
end
always @(negedge clk)if(!reset)begin
 case(cycle%17)3,4,5:enable=0;7:enable=1'bx;8:enable=1'bz;default:enable=1;endcase
 destination_ready=epoch==1&&(cycle%13)>4;
 if(!source_valid||took)begin source_valid=sent<257&&(epoch==0||cycle%7>1);source=input_value(sent);end
 '''+extra_drive+'''
end
always @(posedge clk)if(reset)begin
 sent=0;received=0;cycle=0;held_valid=0;took=0;'''+extra_reset+'''
 #1;if(destination_valid)$fatal(1,"reset leaked an output packet");
end else begin
 if(enable!==1'b1&&source_valid)begin if(source_ready!==0)$fatal(1,"guard consumed input");blocked++;end
 took=source_valid&&source_ready;if(took)sent++;
 '''+extra_count+'''
 if(held_valid&&(!destination_valid||destination!==held))$fatal(1,"output changed while stalled");
 held_valid=destination_valid&&!destination_ready;held=destination;
 if(destination_valid&&destination_ready)begin
  if(received>=sent||destination!==expected(received))$fatal(1,"packet mismatch index=%0d got=%h expected=%h",received,destination,expected(received));received++;
 end
 cycle++;
 if(epoch==1&&received==257)begin
  if(sent!=257||blocked==0)$fatal(1,"packet counts/guard witness incomplete");'''+extra_checks+'''
  $display("PASS pipeline '''+name+''': 257 packets, fixed names, atomic fields, guards, stalls and queued reset");$finish;
 end
 if(cycle>6000)$fatal(1,"timeout sent=%0d received=%0d",sent,received);
end
endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip(),flush=True)


def latency_case(root, split):
    directory=root/('split' if split else 'merged');directory.mkdir()
    source,sv,tb,sim=[directory/n for n in ('latency.pigen','latency.sv','tb.sv','sim')]
    stages=['x<=source;', "y<=x+16'd1;", "x<=x+16'd2;"] if split else ['x<=source;', "y<=x+16'd1;x<=x+16'd2;"]
    source.write_text('module latency(input wire clk,input wire reset,input bit[16] source,output bit[32] buf destination);'
                      'always @(posedge clk)begin pipeline packet begin bit[16] x;bit[16] y;'+
                      ''.join('stage begin '+s+'end ' for s in stages)+'yield {x,y};endpipeline destination<=packet;end endmodule')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','latency',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    tb.write_text('''module tb;logic clk=0,reset=1;logic[15:0]source=16'd17;logic source_valid=0;wire source_ready;
wire[31:0]destination;wire destination_valid;logic destination_ready=1;int cycle=0,accepted=-1;
latency dut(.*);always #5 clk=~clk;
initial begin repeat(3)@(negedge clk);reset=0;source_valid=1;end
always @(negedge clk)if(accepted>=0)source_valid=0;
always @(posedge clk)if(!reset)begin
 if(source_valid&&source_ready)begin if(accepted>=0)$fatal(1,"duplicate source");accepted=cycle;end
 if(destination_valid&&destination_ready)begin
  if(destination!=={16'd19,16'd18}||cycle-accepted!='''+str(len(stages)+1)+''')
   $fatal(1,"stage boundary latency/data: latency=%0d",cycle-accepted);
  $display("PASS pipeline stage count '''+str(len(stages))+''': exact latency %0d",cycle-accepted);$finish;
 end
 cycle++;if(cycle>40)$fatal(1,"latency timeout");
end endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip(),flush=True)


def throughput_case(root):
    directory=root/'throughput';directory.mkdir()
    source,sv,tb,sim=[directory/n for n in ('throughput.pigen','throughput.sv','tb.sv','sim')]
    source.write_text("module throughput(input wire clk,input wire reset,input bit[16] source,output bit[32] buf destination);"
                      "always @(posedge clk)begin pipeline packet begin bit[16] x;bit[16] y;"
                      "stage begin x<=source;y<=source+16'd1;end stage begin x<=x+y;end "
                      "stage begin y<=x^16'h5555;end yield {x,y};endpipeline destination<=packet;end endmodule")
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','throughput',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    tb.write_text("""module tb;logic clk=0,reset=1;logic[15:0]source=0;logic source_valid=0;wire source_ready;
wire[31:0]destination;wire destination_valid;logic destination_ready=1;int sent=0,got=0,cycle=0,last=-1;
throughput dut(.*);always #5 clk=~clk;
initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)if(!reset)begin source_valid=sent<128;source=16'(sent);end
always @(posedge clk)if(!reset)begin
 if(source_valid)begin if(!source_ready)$fatal(1,"pipeline throttled continuous input");sent++;end
 if(destination_valid&&destination_ready)begin
  if(destination!=={16'(2*got+1),16'((2*got+1)^16'h5555)})$fatal(1,"throughput data");
  if(last>=0&&cycle!=last+1)$fatal(1,"pipeline inserted an output bubble");last=cycle;got++;
 end
 cycle++;if(got==128)begin if(sent!=128)$fatal(1,"throughput count");
 $display("PASS pipeline throughput: 128 multi-field packets at one per cycle");$finish;end
 if(cycle>160)$fatal(1,"throughput timeout");
end endmodule
""")
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip(),flush=True)


def negatives(root):
    cases=[
        ('first_read','bit[16] x;',"stage begin x<=x+16'd1;end",'x','pipeline field read before initialization'),
        ('valid_read','bit[16] x;','stage begin x<=16\'(valid(x));end','x','pipeline field read before initialization'),
        ('local_read','bit[16] x;','stage begin bit[16] t=x;x<=t;end','x','pipeline field read before initialization'),
        ('local_self','bit[16] x;','stage begin bit[16] t=t;x<=source;end','x','pipeline local initializer cannot refer to itself'),
        ('late_read','bit[16] x;bit[16] y;','stage begin x<=source;end stage begin x<=y;end','x','pipeline field read before initialization'),
        ('yield','bit[16] x;bit[16] y;','stage begin x<=source;end','y','pipeline yield reads an uninitialized field'),
        ('yield_valid','bit[16] x;bit[16] y;','stage begin x<=source;end','valid(y)','pipeline yield reads an uninitialized field'),
        ('yield_self','bit[16] x;','stage begin x<=source;end','packet','cannot establish the pipeline output identity'),
        ('outside','bit[16] x;','stage begin source<=source;end','x','pipeline stage destination must be a complete pipeline field'),
        ('partial','bit[16] x;','stage begin x[7:0]<=source;end','x','pipeline stage destination must be a complete pipeline field'),
    ]
    for name,fields,stages,result,message in cases:
        source=root/('bad_'+name+'.pigen');sv=source.with_suffix('.sv')
        source.write_text('module bad(input wire clk,input wire reset,input bit[16] source,output bit[16] buf destination);'
                          'always @(posedge clk)begin pipeline packet begin '+fields+stages+' yield '+result+
                          ';endpipeline destination<=packet;end endmodule')
        sv.write_text('preserve output\n')
        diagnostic=run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],root,expect=False)
        assert message in diagnostic,diagnostic
        assert sv.read_text()=='preserve output\n'
    source=root/'ordinary_self.pigen';sv=source.with_suffix('.sv')
    source.write_text('module bad(input wire clk,input wire reset);bit[16] buf x;always @(posedge clk)x<=x;endmodule')
    assert 'buffered transfer cannot source its destination' in run([ROOT/'pigen',source,'-o',sv],root,expect=False)
    print('PASS pipeline diagnostics and ordinary buffered self-consumption rejection',flush=True)


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-pipeline-packets-'));print(root,flush=True)
    for name,profile in PROFILES.items():packet_case(root,name,profile)
    throughput_case(root)
    latency_case(root,False);latency_case(root,True)
    negatives(root)

if __name__=='__main__':main()
