#!/usr/bin/env python3
"""Independent standard-pin witnesses for source-blind inline routing trees."""
from pathlib import Path
import re
import sys
import subprocess
import tempfile
from native_protocol_test import ROOT, run

COMPILER = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT/"pigen").resolve()


def network(root, name, widths, destinations, direct=(), data_kind="bit"):
    directory = root/name
    directory.mkdir()
    source, sv, tb, sim = [directory/n for n in ('network.pigen','network.sv','tb.sv','sim')]
    sinks = sorted(set(destinations))
    sink_widths = {d: widths[destinations.index(d)] for d in sinks}
    declarations = []
    for width in sorted(set(widths)):
        declarations.append(f'''module leaf{width}(input wire clk,input wire reset,input bit[{width}] buf i,output bit[{width}] buf o);
 always @(posedge clk) o<=i; endmodule''')
    ports = ['input wire clk','input wire reset']
    ports += [f'input bit[{w}] buf s{i}' for i,w in enumerate(widths)]
    ports += [f'output bit[{sink_widths[d]}] buf q{d}' for d in sinks]
    declarations.append('module network('+','.join(ports)+');')
    declarations += [f'leaf{w} producer{i}(.clk(clk),.reset(reset),.i(s{i}));' for i,w in enumerate(widths)]
    declarations += [f'leaf{sink_widths[d]} consumer{d}(.clk(clk),.reset(reset),.o(q{d}));' for d in sinks]
    declarations.append('fabric paths begin')
    declarations += [f'producer{i}.o {">" if i in direct else "->"} consumer{d}.i;' for i,d in enumerate(destinations)]
    declarations.append('endfabric endmodule')
    text='\n'.join(declarations)
    if data_kind == 'record':
        text="typedef struct packed {bit[7] tag;bit[9] body;} packet_t;\n"+text.replace('bit[16]','packet_t')
    elif data_kind != 'bit':
        text=text.replace('bit[',data_kind+'[')
    source.write_text(text)
    run([COMPILER,source,'-o',sv],directory)
    run(['verilator','--lint-only','--top-module','network',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    rtl = sv.read_text()
    assert ('pigen_tree_router' in rtl) == (len(direct) != len(widths))
    # User modules keep their original interface; route metadata is private.
    assert not re.search(r'\b(?:input|output)\s+[^;\n]*(?:SOURCE__|__path\b|__handle\b)',rtl)
    diagram = Path(str(sv)+'.svg')
    assert diagram.exists()
    run(['awk','-f',ROOT/'tests/check_fabric_svg.awk',diagram],directory)
    run(['awk','-f',ROOT/'tests/check_fabric_svg_labels.awk',diagram],directory)
    svg = diagram.read_text()
    for i,d in enumerate(destinations):
        assert f'producer{i}.o' in svg and f'consumer{d}.i' in svg
    # Deterministic topology and output, including exact diagram text.
    other = directory/'repeat.sv'
    run([COMPILER,source,'-o',other],directory)
    assert other.read_text() == rtl
    assert Path(str(other)+'.svg').read_text() == svg
    # Every producer keeps its own independent sequence, encoded in payload DATA.
    wires = [f'logic[{w-1}:0] s{i}=0;logic s{i}_valid=0;wire s{i}_ready;logic took{i}=0;int sent{i}=0,received{i}=0;' for i,w in enumerate(widths)]
    wires += [f'wire[{sink_widths[d]-1}:0]q{d};wire q{d}_valid;logic q{d}_ready=0;logic held{d}=0;logic[{sink_widths[d]-1}:0]saved{d};' for d in sinks]
    drive = [f'''if(!s{i}_valid||took{i})begin s{i}_valid=sent{i}<97&&((cycle+{i})%7>1);s{i}={w}'(value({i},sent{i},epoch));end''' for i,w in enumerate(widths)]
    score=[]
    for d in sinks:
        width=sink_widths[d]
        checks=[]
        for i,target in enumerate(destinations):
            if target==d:
                checks.append(f'''{i}:begin if(q{d}!=={width}'(value({i},received{i},epoch)))$fatal(1,"order/data source {i}");received{i}++;end''')
        score.append(f'''if(held{d}&&(!q{d}_valid||q{d}!==saved{d}))$fatal(1,"stalled sink {d} changed");
 held{d}=q{d}_valid&&!q{d}_ready;saved{d}=q{d};
 if(q{d}_valid&&q{d}_ready)begin case(q{d}[3:0]) {' '.join(checks)} default:$fatal(1,"wrong destination {d}");endcase end''')
    count = ''.join(f'took{i}=s{i}_valid&&s{i}_ready;if(took{i})sent{i}++;' for i in range(len(widths)))
    reset = ''.join(f'sent{i}=0;received{i}=0;took{i}=0;' for i in range(len(widths))) + ''.join(f'held{d}=0;' for d in sinks)
    complete = '&&'.join(f'received{i}==97' for i in range(len(widths)))
    readiness = ''.join(f'q{d}_ready=epoch==1&&((cycle+{d})%13>4);' for d in sinks)
    # The first epoch fills the blocked network; shared reset must discard it.
    tb.write_text('''module tb;
logic clk=0,reset=1;int cycle=0,epoch=0;
'''+ '\n'.join(wires)+'''
function automatic logic[128:0]value(input int source,index,e);
 logic[128:0]v;
 v=(129'(e)<<15)|(129'(source*19+7)<<90)|(129'(index*73)<<16)|(129'(index)<<4)|129'(source);
 '''+("if(index%7==2)v[12]=1'bx;if(index%11==3)v[13]=1'bz;" if data_kind=='logic' else '')+''' return v;
endfunction
network dut(.*);always #5 clk=~clk;
initial begin repeat(3)@(negedge clk);reset=0;repeat(180)@(negedge clk);
 reset=1;repeat(3)@(negedge clk);epoch=1;reset=0;end
always @(negedge clk)if(reset)begin '''+''.join(f's{i}_valid=0;' for i in range(len(widths)))+'''end else begin
 cycle++;'''+readiness+'\n'.join(drive)+'''
end
always @(posedge clk)begin
 if(reset)begin '''+reset+'''end
 else begin
 '''+'\n'.join(score)+'\n'+count+'''
 if(epoch==1&&('''+complete+'''))begin $display("PASS inline tree '''+name+''': independent per-source DATA, stalls, queued reset and blind ports");$finish;end
 if(cycle>10000)$fatal(1,"fabric timeout");
 end
end
endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip())
    custom=directory/'custom.svg'
    run([COMPILER,source,'-o',directory/'custom.sv','--diagram',custom],directory)
    assert custom.read_text()==svg
    suppressed=directory/'suppressed.sv'
    run([COMPILER,source,'-o',suppressed,'--no-diagram'],directory)
    assert not Path(str(suppressed)+'.svg').exists()
    return source, sv


def conversion(root, direct):
    directory=root/('conversion_direct' if direct else 'conversion_routed');directory.mkdir()
    source,sv,tb,sim=[directory/n for n in ('convert.pigen','convert.sv','tb.sv','sim')]
    source.write_text('''module widening_source(input wire clk,input wire reset,input int[8] buf i,output int[8] buf o);always @(posedge clk)o<=i;endmodule
module widening_sink(input wire clk,input wire reset,input int[16] buf i,output int[16] buf o);always @(posedge clk)o<=i;endmodule
module network(input wire clk,input wire reset,input int[8] buf s,output int[16] buf result);
 widening_source producer(.clk(clk),.reset(reset),.i(s));widening_sink consumer(.clk(clk),.reset(reset),.o(result));
 fabric begin producer.o '''+('>' if direct else '->')+''' consumer.i;endfabric endmodule''')
    run([COMPILER,source,'-o',sv],directory)
    run(['verilator','--lint-only','--top-module','network',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    tb.write_text('''module tb;
logic clk=0,reset=1,s_valid=0,result_ready=0;logic[7:0]s=0;wire s_ready;
wire[15:0]result;wire result_valid;int sent=0,seen=0,cycle=0;logic took=0;
network dut(.*);always #5 clk=~clk;initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)if(!reset)begin cycle++;result_ready=cycle%11>3;
 if(!s_valid||took)begin s_valid=sent<97;s=8'(sent*37+129);end end
always @(posedge clk)if(!reset)begin
 if(result_valid&&result_ready)begin if(result!==16'($signed(8'(seen*37+129))))$fatal(1,"fabric signed widening");seen++;end
 took=s_valid&&s_ready;if(took)sent++;
 if(seen==97)begin $display("PASS fabric assignment-compatible signed widening");$finish;end
 if(cycle>2000)$fatal(1,"conversion timeout");end endmodule''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip())

def independent_ports(root):
    directory=root/'independent_ports';directory.mkdir()
    source,sv,tb,sim=[directory/n for n in ('ports.pigen','ports.sv','tb.sv','sim')]
    source.write_text('''module independent(input wire clk,input wire reset,input bit[16] buf a,input bit[16] buf b,output bit[16] buf x,output bit[16] buf y);
 always @(posedge clk) begin x<=a;y<=b;end endmodule
module sink(input wire clk,input wire reset,input bit[16] buf i,output bit[16] buf o);always @(posedge clk)o<=i;endmodule
module network(input wire clk,input wire reset,input bit[16] buf a,input bit[16] buf b,output bit[16] buf blocked,output bit[16] buf free_path);
 independent producer(.clk(clk),.reset(reset),.a(a),.b(b));
 sink stopped(.clk(clk),.reset(reset),.o(blocked));sink running(.clk(clk),.reset(reset),.o(free_path));
 fabric begin producer.x -> stopped.i;producer.y > running.i;endfabric endmodule
''')
    run([COMPILER,source,'-o',sv],directory)
    run(['verilator','--lint-only','--top-module','network',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    tb.write_text('''module tb;
logic clk=0,reset=1;logic[15:0]a=0,b=0;logic a_valid=0,b_valid=0;wire a_ready,b_ready;
wire[15:0]blocked,free_path;wire blocked_valid,free_path_valid;logic blocked_ready=0,free_path_ready=1;
int sent_a=0,sent_b=0,seen=0,cycle=0;logic took_a=0,took_b=0;
network dut(.*);always #5 clk=~clk;
initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)if(!reset)begin
 cycle++;if(!a_valid||took_a)begin a_valid=1;a=16'(sent_a);end
 if(!b_valid||took_b)begin b_valid=sent_b<97;b=16'(sent_b+1000);end
end
always @(posedge clk)if(!reset)begin
 if(free_path_valid&&free_path_ready)begin if(free_path!==16'(seen+1000))$fatal(1,"independent output order");seen++;end
 took_a=a_valid&&a_ready;took_b=b_valid&&b_ready;if(took_a)sent_a++;if(took_b)sent_b++;
 if(seen==97)begin if(a_ready||!blocked_valid)$fatal(1,"blocked path did not fill");
 $display("PASS distinct ports on one producer: blocked routed output cannot stall independent direct output");$finish;end
 if(cycle>1500)$fatal(1,"independent port timeout");
end
endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',sim],directory).strip())

def negatives(root):
    leaf='module leaf(input wire clk,input wire reset,input bit[16] buf i,output bit[16] buf o);always @(posedge clk)o<=i;endmodule\n'
    cases={
        'legacy':'fabric old #(parameter integer PAYLOAD_W=16) begin a.o.handle -> b.i; endfabric',
        'source_handle':leaf+'module top(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);fabric begin a.o.handle -> b.i;endfabric endmodule',
        'recognition':leaf+'module top(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);fabric begin a.o -> b.i.origin;endfabric endmodule',
        'parent_storage':leaf+'module top(input wire clk,input wire reset);bit[16] buf x;leaf b(clk,reset);fabric begin x -> b.i;endfabric endmodule',
        'fanout':leaf+'module top(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);leaf c(clk,reset);fabric begin a.o -> b.i;a.o -> c.i;endfabric endmodule',
        'generated':leaf+'module top(input wire clk,input wire reset);if(1)begin:g leaf a(clk,reset);leaf b(clk,reset);fabric begin a.o -> b.i;endfabric end endmodule',
        'mixed_handshake':'module source(output wire[15:0]o);assign o=16\'h1234;endmodule '+leaf+'module top(input wire clk,input wire reset);source a();leaf b(clk,reset);fabric begin a.o -> b.i;endfabric endmodule',
        'clock':leaf+'module top(input wire clk,input wire other,input wire reset);leaf a(.clk(other),.reset(reset));leaf b(.clk(clk),.reset(reset));fabric begin a.o -> b.i;endfabric endmodule',
        'edge':leaf.replace('posedge','negedge')+'module top(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);fabric begin a.o -> b.i;endfabric endmodule',
        'reset':leaf+'module top(input wire clk,input wire reset,input wire other);leaf a(.clk(clk),.reset(other));leaf b(.clk(clk),.reset(reset));fabric begin a.o -> b.i;endfabric endmodule',

    }
    for name,text in cases.items():
        source,output=root/(name+'.pigen'),root/(name+'.sv')
        source.write_text(text);output.write_text('preserved')
        result=subprocess.run([COMPILER,source,'-o',output],text=True,capture_output=True)
        assert result.returncode and output.read_text()=='preserved',(name,result.stdout,result.stderr)
    print('PASS obsolete interfaces, parent storage and consuming fanout reject without publishing output')


def diagram_options(root):
    leaf='module leaf(input wire clk,input wire reset,input bit[16] buf i,output bit[16] buf o);always @(posedge clk)o<=i;endmodule\n'
    units=''.join(f'module net{i}(input wire clk,input wire reset);leaf a(clk,reset);leaf b(clk,reset);fabric begin a.o -> b.i;endfabric endmodule\n' for i in range(2))
    source=root/'multiple.pigen';output=root/'multiple.sv';source.write_text(leaf+units)
    run([COMPILER,source,'-o',output],root)
    assert Path(str(output)+'.net0.svg').exists() and Path(str(output)+'.net1.svg').exists()
    output.write_text('preserved')
    result=subprocess.run([COMPILER,source,'-o',output,'--diagram',root/'ambiguous.svg'],text=True,capture_output=True)
    assert result.returncode and 'exactly one fabric network' in result.stderr and output.read_text()=='preserved'
    empty=root/'empty.pigen';empty.write_text('module empty;endmodule')
    result=subprocess.run([COMPILER,empty,'-o',output,'--diagram',root/'missing.svg'],text=True,capture_output=True)
    assert result.returncode and output.read_text()=='preserved'
    print('PASS multiple/absent diagram selection rejects before output publication')


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-inline-tree-'))
    try:
        network(root,'merge3',[16,16,16],[0,0,0])
        network(root,'mixed',[16,16,16,33,33,129],[0,0,0,1,1,2])
        network(root,'many',[16]*13,[i%5 for i in range(13)])
        network(root,'direct',[16,33],[0,1],direct=(0,1))
        network(root,'signed',[17,17],[0,0],data_kind='int')
        network(root,'records',[16,16],[0,0],data_kind='record')
        network(root,'four_state',[17,17],[0,0],data_kind='logic')
        run(['verilator','--lint-only','--top-module','fabric_tree_router_tb','--timing',ROOT/'rtl/pigen_primitives.sv',ROOT/'tests/fabric_tree_router_tb.sv'],root)
        run(['iverilog','-g2012','-s','fabric_tree_router_tb','-o',root/'router',ROOT/'rtl/pigen_primitives.sv',ROOT/'tests/fabric_tree_router_tb.sv'],root)
        print(run(['vvp',root/'router'],root).strip())
        conversion(root,False)
        conversion(root,True)
        independent_ports(root)
        diagram_options(root)
        negatives(root)
    except BaseException:
        print('Evidence retained at',root)
        raise
    print('PASS source-blind inline fabric tree witnesses:',root)

if __name__=='__main__':main()
