#!/usr/bin/env python3
"""Static procedural initializers run before processes, never on block entry."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT,run


def parameterized_children(root):
    for family in ('axi4_stream','axi4_lite','axi4'):
        directory=root/family;directory.mkdir()
        options='' if family=='axi4_stream' else '[addr=40]' if family=='axi4_lite' else '[addr=40,id=4]'
        member='beat' if family=='axi4_stream' else 'w'
        source=f"""module leaf #(parameter W=32)(input wire clk,input wire reset,input uint[W] {family}{options} buf s,output logic[31:0] wire width);
always_comb begin localparam integer BASE=$bits(s.{member}.data), OFFSET=BASE+1; localparam logic[W-1:0] MASK='1, HALF=MASK>>1; static integer captured=OFFSET-1;width=captured;if ((MASK^HALF)!=(W==32 ? 64'h80000000 : 64'h8000000000000000)) width=0;end endmodule
module local_init(input wire clk,input wire reset,output logic[31:0] wire narrow,output logic[31:0] wire wide);
leaf #(.W(32)) first(.clk(clk),.reset(reset),.width(narrow));
leaf #(.W(64)) second(.clk(clk),.reset(reset),.width(wide));endmodule
"""
        tb='module tb;reg clk=0,reset=1;wire[31:0] narrow,wide;local_init dut(.*);initial begin #1;if(narrow!==32 || wide!==64) $fatal(1,"specialized initializer widths %d %d",narrow,wide);$display("PASS specialized static initializers: independent 32/64-bit native children");$finish;end endmodule'
        pigen,sv,bench,binary=[directory/name for name in ('init.pigen','init.sv','tb.sv','sim')]
        pigen.write_text(source);bench.write_text(tb)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
        run(['verilator','--lint-only','--top-module','local_init',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,bench,ROOT/'rtl/pigen_primitives.sv'],directory)
        print(family+': '+run(['vvp',binary],directory).strip())


def main():
    source="""module local_init(input logic [7:0] data,input logic enable,output logic [31:0] result);
always_comb begin
 typedef struct packed {logic[3:0] hi;logic[3:0] lo;} record_t;
 localparam logic[7:0] FIRST={4'h5,4'ha}, SECOND=FIRST^'1;
 localparam UNTYPED=3, NEXT=UNTYPED+1;
 localparam logic[63:0] WIDE='1, HALF=WIDE>>1;
 localparam logic[63:0] CARRY=32'hffffffff+32'h1;
 localparam logic[7:0] COMPARE=(8'hff+8'h1)==8'h0 ? 8'h55 : 8'haa;
 localparam logic signed[7:0] NEG=-1, SHIFTED=NEG>>>2;
 localparam logic[7:0] NARROW=16'h125a;
 localparam logic[31:0] TRUNCATED=64'h100000004;
 localparam bit[7:0] TWO_X='x, TWO_Z='z;
 localparam int SV_INT=-1;
 localparam int unsigned SV_UINT='1;
 parameter logic[7:0] THIRD=SECOND, FOURTH=THIRD;
 static logic[7:0] seed=FIRST,mask=FOURTH;
 static reg[7:0] reg_seed=8'h33;
 static record_t record_mask='{hi:4'h5,lo:4'ha};
 static logic[7:0] snapshot=data,filled='1;
 logic[CARRY>>32:0] carry_bits;
 logic[7:0] ordered;
 uint[8] marker;
 marker=0;
 ordered=data;
 result[7:0]=data^seed^reg_seed^TWO_X^TWO_Z^8'(SV_INT)^8'(SV_UINT)^8'($bits(SV_INT)-32)^((ordered+8'h1)==8'h0 ? 8'h55 : 8'haa);
 ordered=ordered+8'h1;
 result[15:8]=mask^{record_mask.hi,record_mask.lo}^filled^8'(NEXT-4)^8'(HALF>>56)^8'(SHIFTED)^NARROW^8'(TRUNCATED)^8'(CARRY>>32)^8'($bits(carry_bits))^COMPARE^ordered;
 result[23:16]=snapshot;
 result[31:24]=0;
 if(enable) begin
  static logic[7:0] hidden=data;
  result[31:24]=hidden;
 end
end
endmodule
"""
    oracle=source.replace('module local_init(', 'module oracle(').replace("static record_t record_mask='{hi:4'h5,lo:4'ha};","static record_t record_mask=8'h5a;").replace(' uint[8] marker;',' logic[7:0] marker;')
    tb="""module tb;reg [7:0] data;reg enable=0;wire [31:0] result,expected;integer i;
local_init dut(.*);oracle reference(.data(data),.enable(enable),.result(expected));
initial begin #1;for(i=0;i<1024;i++) begin data=8'(i*31+7);enable=i[0];#1;
if(result!==expected) $fatal(1,"static local initialization i=%0d actual=%h expected=%h",i,result,expected);
if(result[23:16]!==8'hxx || (enable && result[31:24]!==8'hxx)) $fatal(1,"static initializer executed after startup");
end $display("PASS static procedural initialization: 1024 native SV comparisons, startup snapshots, deferred branch and packed records");$finish;end endmodule
"""
    with tempfile.TemporaryDirectory(prefix='pigen-static-local-') as temporary:
        root=Path(temporary)
        pigen,sv,reference,bench,binary=[root/name for name in ('init.pigen','init.sv','oracle.sv','tb.sv','sim')]
        pigen.write_text(source);reference.write_text(oracle);bench.write_text(tb)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root)
        run(['verilator','--lint-only','--top-module','local_init',sv],root)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,reference,bench],root)
        print(run(['vvp',binary],root).strip())
        parameterized_children(root)
        bad=[
          'localparam logic[7:0] A=1,;',
          'localparam logic[7:0] A=1, logic[7:0] B=2;',
          'localparam A=B, B=1;',
          'localparam A=1, A=2;',
          'localparam logic[7:0] A=token;',
          'logic[7:0] value=8\'h5a;',
          'static logic[7:0] value=missing;',
          'static logic[7:0] value=;',
          'static logic[7:0] value=token;',
          'static logic[7:0] wire value=8\'h5a;',
          'for(int i=0;i<2;i++) begin static integer value=i;end',
          'for(int i=0;i<2;i++) begin static integer value=peek(i);end',
          'logic[1:0] data_array;foreach(data_array[i]) begin static integer value=i;end'
        ]
        for declaration in bad:
            pigen.write_text('module bad(input logic[7:0] buf token,output logic[7:0] result);always_comb begin uint[8] marker;'+declaration+'marker=0;result=0;end endmodule')
            sv.write_text('preserved output\n')
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root,expect=False)
            assert sv.read_text()=='preserved output\n'
        print('PASS static initializer diagnostics: explicit lifetime, names, syntax, buffered reads and nets preserve output')


if __name__=='__main__':
    main()
