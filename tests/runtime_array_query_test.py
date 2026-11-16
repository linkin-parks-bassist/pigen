#!/usr/bin/env python3
"""Runtime dimension selection uses fixed types and preserves four-state values."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def token_profiles(root):
    for empty in (False,True):
        value='$size(peek(unused[0]),dimension)' if empty else "$size(peek(bit[3:0][2:1]'(unused)),dimension)"
        for layout in ('direct','when','fsm','child'):
            directory=root/('empty' if empty else 'table')/layout;directory.mkdir(parents=True)
            header='module runtime_query(input wire clk,input wire reset,input logic[7:0] buf dimension,input logic[7:0] buf unused,output logic[31:0] buf result);\n'
            if layout=='child':
                source='module leaf(input wire clk,input wire reset,input logic[31:0] buf incoming,output logic[31:0] buf outgoing);always @(posedge clk) if(!reset) outgoing<=incoming;endmodule\n'
                source+=header+f'leaf forward(.clk(clk),.reset(reset),.incoming({value}),.outgoing(result));endmodule\n'
            elif layout=='fsm':
                source=header+f'fsm @(posedge clk) begin if(reset) goto copy;state copy: begin when(result <= {value}) begin end end end endmodule\n'
            else:
                statement=f'when(result <= {value}) begin end' if layout=='when' else f'result <= {value};'
                source=header+'always @(posedge clk) if(!reset) begin '+statement+' end endmodule\n'
            tb=f"""module tb;
reg clk=0,reset=1;reg [7:0] dimension,unused='x;reg dimension_valid=0,unused_valid=0,result_ready=0;
wire dimension_ready,unused_ready,result_valid;wire [31:0] result;
integer sent=0,received=0,cycle;reg send_fire,receive_fire;reg [31:0] expected[0:255];
runtime_query dut(.*);
initial begin
 #1;clk=1;#1;clk=0;#1;reset=0;
 for(cycle=0;cycle<6000 && received<256;cycle=cycle+1) begin
  dimension_valid=sent<256 && (dimension_valid || cycle%5!=0);
  case(sent%8) 0:dimension=1;1:dimension=2;2:dimension=0;3:dimension=3;4:dimension='x;5:dimension='z;6:dimension=8'hx1;default:dimension=8'h81;endcase
  result_ready=cycle%4!=0 && cycle%11!=0;
  #1;send_fire=dimension_valid&&dimension_ready;receive_fire=result_valid&&result_ready;
  if(unused_ready!==0) $fatal(1,"type-only operand consumed a token");
  if(send_fire) begin
   expected[sent]='x;
   if(!{int(empty)}) case(dimension) 8'd1:expected[sent]=4;8'd2:expected[sent]=2;default:;endcase
   sent=sent+1;
  end
  if(receive_fire) begin
   if(received>=sent || result!==expected[received]) $fatal(1,"query tokens sent=%0d received=%0d result=%h expected=%h",sent,received,result,expected[received]);
   received=received+1;
  end
  clk=1;#1;clk=0;if(send_fire) dimension_valid=0;
 end
 if(sent!=256 || received!=256) $fatal(1,"query dimension consumption lost sent=%0d received=%0d",sent,received);
 $display("PASS runtime query tokens empty={empty} layout={layout}: 256 stalled dimensions, X/Z and unevaluated payload");$finish;
end endmodule
"""
            pigen,sv,bench,binary=[directory/name for name in ('query.pigen','query.sv','tb.sv','sim')]
            pigen.write_text(source);bench.write_text(tb)
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
            run(['verilator','--lint-only','--top-module','runtime_query',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
            run(['iverilog','-g2012','-s','tb','-o',binary,sv,bench,ROOT/'rtl/pigen_primitives.sv'],directory)
            print(run(['vvp',binary],directory).strip())

    for operand in ("bit[3:0][2:1]'(unused)","unused[0]"):
        directory=root/('negative_empty' if operand=='unused[0]' else 'negative_table');directory.mkdir()
        pigen,output=directory/'bad.pigen',directory/'preserved.sv'
        pigen.write_text('module bad(input logic[7:0] buf dimension,input logic[7:0] buf unused,output logic[31:0] wire result);always_comb result=$size('+operand+',dimension);endmodule')
        output.write_text('preserved output\n')
        run([ROOT/'pigen',pigen,'-o',output,'--no-diagram'],directory,expect=False)
        assert output.read_text()=='preserved output\n'
    print('PASS runtime query diagnostics: unpeeked buffered dimensions reject and preserve output')


def native_child_profiles(root,continuous=False):
    for family in ('axi4_stream','axi4_lite','axi4'):
        directory=root/(('assign_forward_' if continuous else 'forward_')+family);directory.mkdir()
        options='' if family=='axi4_stream' else '[addr=40]' if family=='axi4_lite' else '[addr=40,id=4]'
        member='beat' if family=='axi4_stream' else 'w'
        # The query precedes the instance and must see its specialized port type.
        source=f"""module runtime_query(input wire clk,input wire reset,input logic[7:0] dimension,output logic[31:0] wire result,output logic[31:0] wire collection_size);
always_comb result=$size(child.s[3][5].{member}.data,dimension);
always_comb collection_size=$size(child.s,dimension);
leaf #(.W(64)) child(.clk(clk),.reset(reset));
endmodule
module leaf #(parameter W=32)(input wire clk,input wire reset,input uint[W] {family}{options} buf s[3:1][5:6]);endmodule
"""
        if continuous:
            source=source.replace('always_comb result=','assign result=').replace(';\nalways_comb collection_size=',',collection_size=')
        tb="""module tb;reg clk=0,reset=0;reg [7:0] dimension;wire [31:0] result,collection_size;reg [31:0] expected,expected_collection;integer i;
runtime_query dut(.*);
initial begin for(i=0;i<32;i++) begin dimension=8'(i%4);if(i==4) dimension='x;if(i==5) dimension='z;
case(dimension) 1:expected=64;default:expected='x;endcase
case(dimension) 1:expected_collection=3;2:expected_collection=2;default:expected_collection='x;endcase
#1;if(result!==expected) $fatal(1,"forward native inquiry dimension=%h got=%h expected=%h",dimension,result,expected);
if(collection_size!==expected_collection) $fatal(1,"forward native collection query got=%h expected=%h",collection_size,expected_collection);
end $display("PASS forward native query: specialized 64-bit child fields and runtime collection dimensions");$finish;end endmodule
"""
        pigen,sv,bench,binary=[directory/name for name in ('query.pigen','query.sv','tb.sv','sim')]
        pigen.write_text(source);bench.write_text(tb)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
        run(['verilator','--lint-only','--top-module','runtime_query',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,bench,ROOT/'rtl/pigen_primitives.sv'],directory)
        print(('continuous ' if continuous else '')+family+': '+run(['vvp',binary],directory).strip())


def observational_profiles(root):
    for control in ('peek','valid','ready','accepts'):
        directory=root/('observe_'+control);directory.mkdir()
        selector=f'{control}(dimension)' if control!='accepts' else 'accepts(result,dimension)'
        source=f"""module runtime_query(input wire clk,input wire reset,input logic[7:0] buf dimension,input logic[7:0] buf unused,output logic[31:0] buf result);
always @(posedge clk) if(!reset) result <= $size(peek(bit[3:0][2:1]'(unused)),{selector});endmodule
"""
        tb=f"""module tb;reg clk=0,reset=1;reg [7:0] dimension,unused='x;reg dimension_valid=0,unused_valid=0,result_ready=0;
wire dimension_ready,unused_ready,result_valid;wire [31:0] result;reg [31:0] expected;integer phase,cycle,received;
runtime_query dut(.*);
initial begin for(phase=0;phase<8;phase++) begin
 reset=1;#1;clk=1;#1;clk=0;reset=0;dimension_valid={1 if control=='valid' else 0} && phase%2;
 case(phase) 0:dimension=1;1:dimension=2;2:dimension=0;3:dimension=3;4:dimension='x;5:dimension='z;6:dimension=8'hx1;default:dimension=8'hff;endcase
 expected='x;
 if({int(control=='peek')}) case(dimension) 1:expected=4;2:expected=2;default:;endcase
 if({int(control=='valid')} && dimension_valid) expected=4;
 received=0;
 for(cycle=0;cycle<1000 && received<32;cycle++) begin
  result_ready=cycle%4!=0 && cycle%11!=0;#1;
  if(dimension_ready!==0 || unused_ready!==0) $fatal(1,"observational inquiry consumed input tokens");
  if(result_valid && result_ready) begin if(result!==expected) $fatal(1,"observational {control} phase=%0d got=%h expected=%h",phase,result,expected);received++;end
  clk=1;#1;clk=0;
 end
 if(received!=32) $fatal(1,"observational inquiry blocked on an input validity");
end $display("PASS inquiry {control}: 256 stalled observations with no input consumption");$finish;end endmodule
"""
        pigen,sv,bench,binary=[directory/name for name in ('query.pigen','query.sv','tb.sv','sim')]
        pigen.write_text(source);bench.write_text(tb)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
        run(['verilator','--lint-only','--top-module','runtime_query',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,bench,ROOT/'rtl/pigen_primitives.sv'],directory)
        print(run(['vvp',binary],directory).strip())


def continuous_profiles(root):
    directory=root/'continuous_buffer';directory.mkdir()
    source="""module runtime_query(input logic[7:0] buf sample,input logic[1:0] selector,output logic[31:0] wire result,output logic[31:0] wire width);
assign result=$size(sample,selector),width=$bits(sample);endmodule
"""
    tb="""module tb;reg [7:0] sample;reg sample_valid;wire sample_ready;reg [1:0] selector;wire [31:0] result,width;reg [31:0] expected;integer i;
runtime_query dut(.*);
initial begin for(i=0;i<64;i++) begin sample=i[0] ? 'x : 'z;sample_valid=i[1];selector=2'(i%4);if(i==4) selector='x;if(i==5) selector='z;
case(selector) 1:expected=8;default:expected='x;endcase
#1;if(result!==expected || width!==32'd8 || sample_ready!==0) $fatal(1,"continuous inquiry depends on payload or validity");
end $display("PASS continuous query-only buffered operands: 64 X/Z cases, comma bindings and no consumption");$finish;end endmodule
"""
    pigen,sv,bench,binary=[directory/name for name in ('query.pigen','query.sv','tb.sv','sim')]
    pigen.write_text(source);bench.write_text(tb)
    run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','runtime_query',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,bench,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',binary],directory).strip())
    header='module bad(input logic[7:0] buf sample,output logic[31:0] wire result);'
    negatives=[header+'assign result=$size(sample,sample);endmodule',
               header.replace('wire result','buf result')+'assign result=$bits(sample);endmodule',
               header+'assign sample=$bits(sample);endmodule',
               header+'assign result=$bits(sample,1);endmodule']
    for i,text in enumerate(negatives):
        pigen.write_text(text);sv.write_text('preserved output\n')
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory,expect=False)
        assert sv.read_text()=='preserved output\n'
    pigen.write_text('module ordinary(input logic[7:0] sample,output wire[31:0] result);assign result=$bits(sample);endmodule')
    run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
    assert '$bits(sample)' in sv.read_text()
    run(['verilator','--lint-only','--top-module','ordinary',sv],directory)
    print('PASS continuous inquiry diagnostics preserve output; ordinary SV queries retain preservation')


def main():
    functions=['$left','$right','$low','$high','$size','$increment']
    selectors=['dimension',"(dimension & '1)","(dimension ^ '0)","64'(dimension[2:0])"]
    queries=[(function,selector) for selector in selectors for function in functions]
    ports=['input logic signed [63:0] dimension']+[f'output logic [31:0] q{i}' for i in range(len(queries))]
    ports+=['output logic [31:0] empty_size','output logic [31:0] typed_size']
    declarations='logic [3:0][2:1] cells[-2:1][7:5]; logic scalar;\n'
    source='module runtime_query('+','.join(ports)+'); always_comb begin\n'+declarations
    source+='uint[8] marker;marker=0;\n'
    source+='\n'.join(f'q{i}={function}(cells,{selector});' for i,(function,selector) in enumerate(queries))
    source+='\nempty_size=$size(scalar,dimension);typed_size=$size(logic[3:0][2:1],dimension);end endmodule\n'
    oracle='module oracle('+','.join(ports)+');\n'+declarations+'always_comb begin\n'
    # Icarus cannot execute a native query with a runtime dimension. Its native
    # constant queries plus a plain case form an independent four-state oracle.
    for i,(function,selector) in enumerate(queries):
        oracle+=f'case({selector})\n'
        oracle+='\n'.join(f'{d}: q{i}={function}(cells,{d});' for d in range(1,5))
        oracle+=f"\ndefault: q{i}='x;endcase\n"
    oracle+="empty_size='x;case(dimension) 1:typed_size=4;2:typed_size=2;default:typed_size='x;endcase end endmodule\n"
    tb='module tb;reg signed [63:0] dimension;integer i;\n'
    tb+='\n'.join(f'wire [31:0] actual{i},expected{i};' for i in range(len(queries)))
    tb+='wire [31:0] actual_empty,expected_empty,actual_type,expected_type;\n'
    for module,prefix in [('runtime_query','actual'),('oracle','expected')]:
        tb+=module+' dut_'+prefix+'(.dimension(dimension),.empty_size('+prefix+'_empty),.typed_size('+prefix+'_type),'+','.join(f'.q{i}({prefix}{i})' for i in range(len(queries)))+');\n'
    checks='\n'.join(f'if(actual{i}!==expected{i}) $fatal(1,"runtime query {i} dimension=%h got=%h expected=%h",dimension,actual{i},expected{i});' for i in range(len(queries)))
    checks+='\nif(actual_empty!==expected_empty || actual_type!==expected_type) $fatal(1,"rank-zero or type-only query");'
    tb+='initial begin for(i=0;i<256;i++) begin\n'
    tb+="case(i%8) 0:dimension=64'(i%9-2);1:dimension=64'h100000001;2:dimension=64'h8000000000000001;3:dimension='x;4:dimension='z;5:dimension=64'h00000000000000x1;6:dimension=64'hfffffffffffffffx;default:dimension=64'(i%7);endcase\n#1;\n"
    tb+=checks+'\nend\n$display("PASS runtime array queries: 24 native query tables, 256 wide/signed/X/Z selectors and rank-zero operands");$finish;end endmodule\n'
    with tempfile.TemporaryDirectory(prefix='pigen-runtime-query-') as temporary:
        root=Path(temporary)
        pigen,sv,reference,bench,binary=[root/name for name in ('query.pigen','query.sv','oracle.sv','tb.sv','sim')]
        pigen.write_text(source);reference.write_text(oracle);bench.write_text(tb)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root)
        run(['verilator','--lint-only','--top-module','runtime_query',sv],root)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,reference,bench],root)
        print(run(['vvp',binary],root).strip())
        token_profiles(root)
        native_child_profiles(root)
        native_child_profiles(root,continuous=True)
        observational_profiles(root)
        continuous_profiles(root)


if __name__=='__main__':
    main()
