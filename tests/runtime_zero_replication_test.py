#!/usr/bin/env python3
"""Empty fields retain ordered reads and token consumption despite zero bits."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT,run


def check(directory,source_text,tb_text,expect_emitted_empty=True):
    source,sv,tb=(directory/name for name in ('zero.pigen','zero.sv','tb.sv'))
    source.write_text(source_text);tb.write_text(tb_text)
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    if expect_emitted_empty: assert '{0{' in sv.read_text()
    run(['verilator','--lint-only','--timing','--top-module','tb',sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    binary=directory/'sim'
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    print(run(['vvp',binary],directory).strip())


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-runtime-zero-') as temporary:
        root=Path(temporary);directory=root/'ordered';directory.mkdir()
        check(directory,'''module zero(input logic [7:0] data,input logic [7:0] ignored,output logic [31:0] result);
always_comb begin
 uint[8] marker; logic [7:0] carry;
 marker=0; carry=data;
 result[7:0]={(({0{ignored}})),carry,{0{ignored^8'hff}},{0{carry}}};
 carry=~carry;
 result[15:8]={carry[7:4],{0{ignored}},carry[3:0]};
 result[23:16]={2{{0{ignored}},carry[3:0]}};
 result[31:24]={{0{{0{ignored}},carry}},data};
end
endmodule
''','''module tb;
reg [7:0] data,ignored; wire [31:0] result; reg [31:0] expected; reg [7:0] inverted; integer i;
zero dut(.*);
initial begin
 for(i=0;i<1024;i=i+1) begin
  data=8'(i*31); ignored=i[0] ? 8'hxx : 8'hzz; inverted=~data;
  expected={data,inverted[3:0],inverted[3:0],inverted,data};
  #1; if(result!==expected) $fatal(1,"empty-field ordered reads i=%0d got=%h expected=%h",i,result,expected);
 end
 $display("PASS runtime zero fields: 1024 ordered reads, X/Z operands and nested repetition");$finish;
end
endmodule
''')
        directory=root/'foreach';directory.mkdir()
        check(directory,"""module zero(input logic [7:0] data,input logic [7:0] ignored,input logic row,output logic [7:0] result);
always_comb begin
 uint[8] marker; logic [7:0] cells[0:1][3:4];
 marker=0; cells[0][3]=data;cells[0][4]=~data;cells[1][3]=data;cells[1][4]=8'h3c;
 result=0;
 foreach(cells[{{0{ignored}},row}][j]) result^=cells[row][j];
end
endmodule
""","""module tb;
reg [7:0] data,ignored;reg row;wire [7:0] result;reg [7:0] expected;integer i;
zero dut(.*);
initial begin
 for(i=0;i<1024;i=i+1) begin
  data=8'(i*31);row=i[0];ignored=i[1] ? 8'hxx : 8'hzz;
  expected=row ? data^8'h3c : 8'hff;
  #1;if(result!==expected) $fatal(1,"foreach empty header row=%b data=%h got=%h expected=%h",row,data,result,expected);
 end
 $display("PASS analysis-only foreach target: 1024 selected rows with ignored X/Z indices");$finish;
end
endmodule
""",expect_emitted_empty=False)
        for peek,hierarchy in ((False,False),(True,False),(False,True),(True,True)):
            directory=root/(('peek' if peek else 'consume')+('_child' if hierarchy else ''));directory.mkdir()
            ignored='peek(discarded)' if peek else 'discarded'
            value='{{0{'+ignored+'}},kept,{0{'+ignored+'}}}'
            source=f'''module zero(input wire clk,input wire reset,input uint[8] buf kept,input uint[8] buf discarded,output uint[8] buf result);
always @(posedge clk) if(!reset) result <= uint[8]'({value});
endmodule
'''
            if hierarchy:
                source=f"""module leaf(input wire clk,input wire reset,input uint[8] buf incoming,output uint[8] buf outgoing);
always @(posedge clk) if(!reset) outgoing <= incoming;
endmodule
module zero(input wire clk,input wire reset,input uint[8] buf kept,input uint[8] buf discarded,output uint[8] buf result);
leaf forward(.clk(clk),.reset(reset),.incoming(uint[8]'({value})),.outgoing(result));
endmodule
"""
            check(directory,source,f'''module tb;
reg clk=0,reset=1; reg [7:0] kept,discarded; reg kept_valid=0,discarded_valid=0,result_ready=0;
wire kept_ready,discarded_ready,result_valid; wire [7:0] result;
integer k=0,d=0,r=0,cycle; reg kfire,dfire,rfire; reg [7:0] expected[0:255];
zero dut(.*);
initial begin
 #1; clk=1; #1; clk=0; #1; reset=0;
 for(cycle=0;cycle<6000 && r<256;cycle=cycle+1) begin
  kept_valid=k<256 && (kept_valid || cycle%5!=0);
  discarded_valid={0 if peek else 1} && d<256 && (discarded_valid || cycle%7==0);
  kept=8'(k*31+17);discarded=8'(d*17+31);result_ready=cycle%4!=0 && cycle%11!=0;
  #1; kfire=kept_valid&&kept_ready;dfire=discarded_valid&&discarded_ready;rfire=result_valid&&result_ready;
  if(kfire) begin expected[k]=kept;k=k+1;end
  if(dfire) d=d+1;
  if(rfire) begin
   if(r>=k || result!==expected[r]) $fatal(1,"empty token payload r=%0d k=%0d data=%h",r,k,result);
   r=r+1;
  end
  if({1 if peek else 0} && discarded_ready!==0) $fatal(1,"ignored peek consumed a token");
  clk=1; #1; clk=0;
  if(kfire) kept_valid=0;
  if(dfire) discarded_valid=0;
 end
 if(r!=256 || k!=256 || d!={0 if peek else 256}) $fatal(1,"empty token dependencies lost k=%0d d=%0d r=%0d",k,d,r);
 $display("PASS runtime zero {'peek' if peek else 'consume'} child={hierarchy}: 256 independent stalled tokens and deduplicated ignored reads");$finish;
end
endmodule
''')

        header='module bad(input wire clk,input wire reset,input uint[8] buf kept,input uint[8] buf discarded,output uint[8] buf result);'
        bad_values=['{0{discarded}}','{{0{discarded}}}','{{{0{discarded}}},kept}',
                    '{{0{missing}},kept}',"{{discarded{1'b0}},kept}",'{{(-1){discarded}},kept}',
                    '{{0{unknown_call()}},kept}']
        source=root/'bad.pigen';output=root/'bad.sv'
        for value in bad_values:
            source.write_text(header+"always @(posedge clk) if(!reset) result <= uint[8]'("+value+"); endmodule")
            output.write_text('preserved output')
            run([ROOT/'pigen',source,'-o',output,'--no-diagram'],root,expect=False)
            assert output.read_text()=='preserved output'
        source.write_text("module bad(input uint[8] buf discarded,output uint[8] wire result); always_comb result=uint[8]'({{0{discarded}},8'h55}); endmodule")
        run([ROOT/'pigen',source,'-o',output,'--no-diagram'],root,expect=False)
        assert output.read_text()=='preserved output'
        print('PASS runtime zero rejection: invalid empty contexts, names/counts/calls and unpeeked buffered combinational reads')


if __name__=='__main__':
    main()
