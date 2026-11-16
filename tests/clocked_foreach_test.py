#!/usr/bin/env python3
"""Fixed clocked iteration: native-pin queues and an independent SV oracle."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from native_protocol_test import ROOT, run
from native_array_test import case


def native_transport(root):
    for family in ('axi4_stream','axi4_lite','axi4'):
        for mode,form in (('direct','flat'),('aggregate','nested'),
                          ('projection','qualified'),('parameter','flat'),
                          ('fabric_child','fsm'),('scalar_binding','nested')):
            directory=root/f'{family}_{mode}'
            directory.mkdir()
            case(directory,family,mode,native_alias=True,clocked_foreach=form)
    print('PASS 18 clocked native foreach profiles',flush=True)


def ordered_iteration(root):
    directory=root/'ordered'
    directory.mkdir()
    source,sv,oracle,tb,binary=[directory/name for name in ('walk.pigen','walk.sv','oracle.sv','tb.sv','sim')]
    text='''module walk(input wire clk,input wire reset,input wire[7:0] values[-2:0][3:2],output logic[23:0] result);
always @(posedge clk)if(!reset)begin
 foreach(values[i,j])if(i!=-1||j!=3)result[7:0]<=values[i][j]^8'(i*17+j);
 foreach(values[i])foreach(values[,j])result[15:8]<=values[i][j]^8'(i*23+j);
 foreach(values[,j])result[23:16]<=values[-2][j]^8'(j*19);
end endmodule
'''
    source.write_text(text)
    oracle.write_text(text.replace('module walk(', 'module oracle('))
    tb.write_text('''module tb;logic clk=0,reset=1;logic[7:0] values[-2:0][3:2];wire[23:0] result,expected;
integer trial,i,j;always #5 clk=~clk;walk dut(.*);oracle reference_dut(.clk(clk),.reset(reset),.values(values),.result(expected));
initial begin for(trial=0;trial<1024;trial++)begin @(negedge clk);reset=(trial%47)==0;
 for(i=-2;i<=0;i++)for(j=3;j>=2;j--)begin values[i][j]=8'(trial*37+i*11+j);if(trial%7==0)values[i][j][1:0]=2'bxz;end
 @(posedge clk);#1;if(result!==expected)$fatal(1,"clocked foreach declaration order mismatch trial=%0d actual=%h expected=%h",trial,result,expected);
end $display("PASS ordered clocked foreach: 1024 native SV edges, signed bounds, omissions, nesting, X/Z and reset");$finish;end
endmodule
''')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','walk',sv],directory)
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,oracle,tb],directory)
    print(run(['vvp',binary],directory).strip(),flush=True)


def loop_exits(root):
    directory=root/'loop_exits'
    directory.mkdir()
    source,sv,oracle,tb,binary=[directory/name for name in ('walk.pigen','walk.sv','oracle.sv','tb.sv','sim')]
    ports=('input wire clk,input wire reset,input logic skip,input logic halt,'
           'input wire[7:0] values[-2:0][3:2],'
           'output logic[7:0] before_value[0:2],after_value[0:2],middle[0:2],tail[0:2],'
           'output logic[7:0] outside_value')
    first=('before_value[i+2]<=values[i][j];'
           'if(j==3&&skip)continue;if(i==-1&&halt)break;'
           "after_value[i+2]<=values[i][j]^8'h55;")
    inner=("if(j==3&&skip)continue;middle[i+2]<=values[i][j]^8'h33;if(halt)break;")
    text=(f'module walk({ports});always @(posedge clk)if(!reset)begin '
          'foreach(values[i,j])begin '+first+'end '
          'outside_value<=values[-2][3];foreach(values[i])begin '
          'foreach(values[,j])begin '+inner+'end tail[i+2]<=values[i][2];end end endmodule')
    source.write_text(text)
    # Icarus lacks native break/continue. Lexical disables independently
    # express the same loop targets without reconstructing Pigen guards.
    reference=(f'module oracle({ports});always @(posedge clk)if(!reset)begin '
               'begin : outer_exit foreach(values[i,j])begin : outer_iteration '+
               first.replace('continue;','disable outer_iteration;').replace('break;','disable outer_exit;')+
               'end end outside_value<=values[-2][3];foreach(values[i])begin '
               'begin : inner_exit foreach(values[,j])begin : inner_iteration '+
               inner.replace('continue;','disable inner_iteration;').replace('break;','disable inner_exit;')+
               'end end tail[i+2]<=values[i][2];end end endmodule')
    oracle.write_text(reference)
    tb.write_text('''module tb;logic clk=0,reset=1,skip=0,halt=0;logic[7:0] values[-2:0][3:2];
wire[7:0] before_value[0:2],after_value[0:2],middle[0:2],tail[0:2],outside_value;
wire[7:0] eb[0:2],ea[0:2],em[0:2],et[0:2],eo;integer trial,i,j;always #5 clk=~clk;
walk dut(.*);oracle reference_dut(.clk(clk),.reset(reset),.skip(skip),.halt(halt),.values(values),
.before_value(eb),.after_value(ea),.middle(em),.tail(et),.outside_value(eo));
initial begin for(trial=0;trial<1024;trial++)begin @(negedge clk);reset=trial%47==0;
case(trial%4)0:skip=0;1:skip=1;2:skip=1'bx;3:skip=1'bz;endcase
case((trial/4)%4)0:halt=0;1:halt=1;2:halt=1'bx;3:halt=1'bz;endcase
for(i=-2;i<=0;i++)for(j=3;j>=2;j--)begin values[i][j]=8'(trial*37+i*11+j);if(trial%7==0)values[i][j][1:0]=2'bxz;end
@(posedge clk);#1;for(i=0;i<3;i++)if(before_value[i]!==eb[i]||after_value[i]!==ea[i]||middle[i]!==em[i]||tail[i]!==et[i])
$fatal(1,"clocked loop exit mismatch trial=%0d lane=%0d",trial,i);
if(outside_value!==eo)$fatal(1,"break escaped its lexical loop");
end $display("PASS clocked loop exits: 1024 edges, multidimensional/nested targets, four-state guards and prior/later writes");$finish;end endmodule
''')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','walk',sv],directory)
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,oracle,tb],directory)
    print(run(['vvp',binary],directory).strip(),flush=True)
    oracle.write_text(text.replace('module walk(', 'module oracle('))
    native_tb=directory/'native_tb.sv'
    native_tb.write_text(tb.read_text().replace("2'bxz","2'b10").replace("1'bx","1'b0").replace("1'bz","1'b1"))
    run(['verilator','--binary','--timing','--top-module','tb','-j','2',
         '--Mdir',directory/'obj','-o',directory/'native_sim',sv,oracle,native_tb],directory)
    assert 'PASS clocked loop exits' in run([directory/'native_sim'],directory)
    for family in ('axi4_stream','axi4_lite','axi4'):
        for mode in ('direct','aggregate','fabric_child'):
            child=root/f'exits_{family}_{mode}'
            child.mkdir()
            case(child,family,mode,native_alias=True,clocked_foreach='exits')


def negatives(root):
    directory=root/'negative'
    directory.mkdir()
    source,output=directory/'bad.pigen',directory/'bad.sv'
    header=('module bad(input wire clk,input wire reset,input uint[8] axi4_stream buf s[2],'
            'output uint[8] axi4_stream buf m[2]);always @(posedge clk)if(!reset)')
    for body in ('foreach(s[i])i<=0;', 'foreach(s[s])m[0]<=s[0];',
                 'foreach(s[i,j])m[0]<=s[0];', 'foreach(s[,])m[0]<=s[0];',
                 'begin foreach(s[i])m[i]<=s[i];m[i]<=s[0];end',
                 'foreach(s[i,i])m[0]<=s[0];',
                 'break;', 'continue;', 'foreach(s[i])begin break end'):
        source.write_text(header+body+'endmodule')
        output.write_text('preserved output\n')
        run([ROOT/'pigen',source,'-o',output,'--no-diagram'],directory,expect=False)
        assert output.read_text()=='preserved output\n'
    print('PASS clocked foreach diagnostics: immutable/scoped indices, rank and names',flush=True)


def example(root):
    directory=root/'example'
    directory.mkdir()
    text=(ROOT/'examples/native_axi_foreach.pigen').read_text()
    for module,family,config,parameters in (
        ('native_axis_foreach','axi4_stream','keep=1,last=1',''),
        ('native_axil_foreach','axi4_lite','addr=48',',.ADDRESS_BITS(48)'),
        ('native_axi4_foreach','axi4','addr=48,id=8',',.ADDRESS_BITS(48),.ID_BITS(8)')):
        source,sv=directory/f'{module}.pigen',directory/f'{module}.sv'
        source.write_text(text+f'\nmodule override(input wire clk,input wire reset,'
                          f'input uint[64] {family}[{config}] fifo[3] s[2],'
                          f'output uint[64] {family}[{config}] skid m[2]);'
                          f'{module} #(.LANES(2),.DATA_W(64){parameters}) '
                          'leaf(.clk(clk),.reset(reset),.s(s),.m(m));endmodule\n')
        run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
        for top in (module,'override'):
            run(['verilator','--lint-only','--top-module',top,sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    print('PASS clocked foreach examples: defaults and per-instance lane/data/address/ID overrides',flush=True)


def fsm_termination(root):
    directory=root/'fsm_termination'
    directory.mkdir()
    source,sv,oracle,tb=[directory/name for name in ('relay.pigen','relay.sv','oracle.sv','tb.sv')]
    ports=('input wire clk,input wire reset,input wire stop,input wire restart,input wire jump,'
           'input wire[7:0] selector,input wire[7:0] values[-2:0],'
           'output logic[7:0] result[-2:0],output logic[7:0] tail[-2:0]')
    selection=("unique case(selector) 0:result[i]<=values[i]^8'h55;"
               "(i==0 ? selector : 8'd1):result[i]<=values[i]^8'haa;"
               "default:result[i]<=values[i];endcase ")
    source.write_text(f'module relay({ports});fsm @(posedge clk)begin if(reset)goto run;'
                      'if(stop)goto idle;state run:foreach(values[i])begin '+selection+
                      'if(jump&&i==-1)goto idle;tail[i]<=values[i];end '
                      'state idle:if(restart)goto run;end endmodule')
    oracle.write_text(f'module relay({ports});localparam RUN=0,IDLE=1;logic state;'
                      'always @(posedge clk)begin bit live;live=1;'
                      'if(reset)begin state<=RUN;live=0;end '
                      'if(live&&stop)begin state<=IDLE;live=0;end '
                      'if(live&&state==RUN)foreach(values[i])if(live)begin '+selection+
                      'if(jump&&i==-1)begin state<=IDLE;live=0;end '
                      'if(live)tail[i]<=values[i];end '
                      'if(live&&state==IDLE&&restart)state<=RUN;end endmodule')
    tb.write_text('''module tb;logic clk=0,reset=1,stop=0,restart=0,jump=0;logic[7:0] selector=2;
logic[7:0] values[-2:0];wire[7:0] result[-2:0],tail[-2:0];integer i,trial,mode;
always #5 clk=~clk;relay dut(.*);
initial begin if(!$value$plusargs("MODE=%d",mode))mode=0;
 for(i=-2;i<=0;i++)values[i]=0;repeat(3)@(negedge clk);reset=0;
 for(trial=0;trial<128;trial++)begin
  for(i=-2;i<=0;i++)values[i]=8'(trial*37+i*11);
  reset=trial%19==0;stop=trial%17==0;restart=trial%3==0;jump=trial%4==0;
  @(posedge clk);#1;@(negedge clk);
 end
 reset=1;stop=0;restart=0;jump=0;@(negedge clk);reset=0;selector=0;
 jump=mode==1;reset=mode==3;stop=mode==4;
 @(posedge clk);#1;$display("PASS foreach FSM entered-case checks mode=%0d",mode);$finish;
end endmodule
''')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    # Compare complete observable state, independently of assertion parity.
    functional=directory/'functional.sv'
    functional.write_text(oracle.read_text().replace('module relay(', 'module reference(')+'\n'+
                         tb.read_text().replace('relay dut(.*);',
                           'wire[7:0] expected[-2:0],expected_tail[-2:0];relay dut(.*);'
                           'reference ref_dut(.clk(clk),.reset(reset),.stop(stop),.restart(restart),'
                           '.jump(jump),.selector(selector),.values(values),.result(expected),.tail(expected_tail));')
                         .replace('@(posedge clk);#1;@(negedge clk);',
                           '@(posedge clk);#1;for(i=-2;i<=0;i++)begin '
                           'if(result[i]!==expected[i]||tail[i]!==expected_tail[i])'
                           '$fatal(1,"foreach FSM first-goto effects mismatch");end @(negedge clk);'))
    run(['verilator','--lint-only','--top-module','relay',sv],directory)
    run(['verilator','--binary','--assert','--timing','--top-module','tb','-j','2',
         '--Mdir',directory/'functional_obj','-o',directory/'functional_sim',sv,functional],directory)
    run([directory/'functional_sim','+MODE=1'],directory)
    for name,design in (('generated',sv),('native',oracle)):
        run(['verilator','--binary','--assert','--timing','--top-module','tb','-j','2',
             '--Mdir',directory/f'{name}_obj','-o',directory/f'{name}_sim',design,tb],directory)
        for mode in (1,2,3,4):
            result=subprocess.run([directory/f'{name}_sim',f'+MODE={mode}'],cwd=directory,text=True,
                                  stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert (result.returncode!=0)==(mode==2),(name,mode,result.stdout)
    print('PASS foreach FSM: first-goto effects and native assertion parity across iteration, reset and stop',flush=True)


def exit_qualification(root):
    directory=root/'exit_qualification'
    directory.mkdir()
    source,sv,oracle,tb=[directory/name for name in ('relay.pigen','relay.sv','oracle.sv','tb.sv')]
    ports=('input wire clk,input wire reset,input wire restart,input wire skip,input wire halt,input wire clash,'
           'input wire[7:0] selector,input wire[7:0] values[-2:0],'
           'output logic[7:0] result[-2:0],output logic[7:0] outside_value')
    first=("unique case(selector)0:begin result[i]<=values[i];if(skip&&i==-2)continue;end "
           "(clash&&i==-2 ? selector : 8'd1):result[i]<=~values[i];default:result[i]<=values[i];endcase ")
    second=("unique case(selector)0:result[i]<=values[i]^8'h55;"
            "(i==0 ? selector : 8'd1):result[i]<=values[i]^8'haa;default:result[i]<=values[i];endcase ")
    source.write_text(f'module relay({ports});fsm @(posedge clk)begin if(reset)goto run;'
                      'state run:begin foreach(values[i])begin '+first+
                      'if(halt&&i==-2)break;if(skip&&i==-2)goto idle;'+second+
                      'end outside_value<=values[0];end state idle:if(restart)goto run;end endmodule')
    oracle.write_text(f'module relay({ports});localparam RUN=0,IDLE=1;logic state;'
                      'always @(posedge clk)begin bit live,loop_live,iteration_live;live=1;loop_live=1;iteration_live=1;'
                      'if(reset)begin state<=RUN;live=0;end '
                      'if(live&&state==RUN)begin foreach(values[i])if(live&&loop_live)begin iteration_live=1;'+
                      first.replace('continue;','iteration_live=0;')+
                      'if(iteration_live)begin if(halt&&i==-2)loop_live=0;if(loop_live)begin '
                      'if(skip&&i==-2)begin state<=IDLE;live=0;end if(live)begin '+second+
                      'end end end end if(live)outside_value<=values[0];end '
                      'if(live&&state==IDLE&&restart)state<=RUN;end endmodule')
    tb.write_text('''module tb;logic clk=0,reset=1,restart=0,skip=0,halt=0,clash=0;logic[7:0] selector=2;
logic[7:0] values[-2:0];wire[7:0] result[-2:0],outside_value;integer i,trial,mode;
always #5 clk=~clk;relay dut(.*);
initial begin if(!$value$plusargs("MODE=%d",mode))mode=0;for(i=-2;i<=0;i++)values[i]=0;
repeat(3)@(negedge clk);reset=0;
for(trial=0;trial<128;trial++)begin for(i=-2;i<=0;i++)values[i]=8'(trial*37+i*11);
reset=trial%19==0;restart=trial%3==0;skip=trial%4==0;halt=trial%7==0;
@(posedge clk);#1;@(negedge clk);end
reset=1;skip=0;halt=0;restart=0;@(negedge clk);reset=0;selector=0;
clash=mode==1;skip=mode==1||mode==3||mode==4;halt=mode==2;reset=mode==5;
if(mode==4)selector=2;
@(posedge clk);#1;$display("PASS loop exit qualification mode=%0d",mode);$finish;end endmodule
''')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    functional=directory/'functional.sv'
    bench=tb.read_text().replace('relay dut(.*);',
        'wire[7:0] expected[-2:0],expected_outside;relay dut(.*);'
        'reference ref_dut(.clk(clk),.reset(reset),.restart(restart),.skip(skip),.halt(halt),.clash(clash),'
        '.selector(selector),.values(values),.result(expected),.outside_value(expected_outside));')
    bench=bench.replace('endmodule',
        'always @(negedge clk)begin for(integer lane=-2;lane<=0;lane++)'
        'if(result[lane]!==expected[lane])$fatal(1,"exit/goto changed ordered writes");'
        'if(outside_value!==expected_outside)$fatal(1,"exit/goto changed enclosing reachability");end endmodule')
    functional.write_text(oracle.read_text().replace('module relay(', 'module reference(')+'\n'+bench)
    run(['verilator','--binary','--assert','--timing','--top-module','tb','-j','2',
         '--Mdir',directory/'functional_obj','-o',directory/'functional_sim',sv,functional],directory)
    run([directory/'functional_sim','+MODE=2'],directory)
    for name,design in (('generated',sv),('native',oracle)):
        run(['verilator','--binary','--assert','--timing','--top-module','tb','-j','2',
             '--Mdir',directory/f'{name}_obj','-o',directory/f'{name}_sim',design,tb],directory)
        for mode in (1,2,3,4,5,6):
            result=subprocess.run([directory/f'{name}_sim',f'+MODE={mode}'],cwd=directory,text=True,
                                  stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert (result.returncode!=0)==(mode in (1,3,6)),(name,mode,result.stdout)
    print('PASS exit/qualification FSM: entered checks, skipped gotos, break/continue targets and reset',flush=True)


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-clocked-foreach-'))
    print(root,flush=True)
    try:
        ordered_iteration(root)
        loop_exits(root)
        negatives(root)
        example(root)
        fsm_termination(root)
        exit_qualification(root)
        native_transport(root)
    except BaseException:
        print(f'Clocked foreach evidence retained in {root}',flush=True)
        raise
    else:
        shutil.rmtree(root)


if __name__=='__main__':
    main()
