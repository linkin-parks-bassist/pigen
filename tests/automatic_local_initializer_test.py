#!/usr/bin/env python3
"""Automatic block entry semantics compared with a native automatic function."""
from pathlib import Path
import tempfile
import re
from native_protocol_test import ROOT, run


def main():
    body='''
 typedef struct packed {logic[3:0] unknown;bit[3:0] zero;} mixed_t;
 typedef mixed_t[1:0] pair_t;
 automatic logic[7:0] snapshot=data,dependent=snapshot^8'h5a;
 automatic bit[7:0] cleared;
 automatic logic[7:0] unknown;
 automatic mixed_t record_default;
 automatic pair_t pair_default;
 automatic logic[7:0] array_default[-1:1][2:1];
 automatic bit[7:0] self=self+1;
 automatic uint[8] numeric=uint[8]'(data);
 int sum;
 result=0;sum=0;
 result[7:0]=snapshot;result[15:8]=dependent;
 result[23:16]=cleared;result[31:24]=unknown;
 result[39:32]=record_default;result[55:40]=pair_default;
 result[63:56]=self;
 foreach(array_default[i,j]) begin
  result[71:64]=array_default[i][j];array_default[i][j]=data;
 end
 for(int i=0;i<4;i++) begin
  automatic int each=i;
  automatic bit[7:0] fresh;
  each+=int'(data);sum+=each+int'(fresh);fresh=8'hff;
  if(i==1) continue;
  sum+=1;
 end
 result[103:72]=32'(sum);result[111:104]=numeric;
 if(enable) begin
  automatic logic[7:0] branch=data;
  result[119:112]=branch;branch=0;
 end
 cleared=8'hff;unknown=0;record_default='0;pair_default='0;
 snapshot=0;dependent=0;self=9;numeric=0;
'''
    source='module local_auto(input logic[7:0] data,input logic enable,output logic[127:0] result);always_comb begin'+body+'end endmodule\n'
    # Icarus supports automatic function lifetimes, but not explicit automatic
    # variable overrides in a static process. Function locals supply the oracle.
    body=' typedef bit[7:0] numeric_t;\n'+body
    native=body.replace('automatic ','').replace('uint[8]','numeric_t').replace('result','calculate')
    first=native.index(' for(int i=0;i<4;i++) begin')
    last=native.index(' calculate[103:72]',first)
    native=native[:first]+" for(int i=0;i<4;i++) begin sum+=entry(data,i);if(i!=1) sum+=1;end\n"+native[last:]
    helpers="""function automatic int entry(input logic[7:0] data,input int i);
 bit[7:0] fresh;int each=i;
 each+=int'(data);entry=each+int'(fresh);fresh=8'hff;
endfunction
"""
    native=re.sub(r'\bint\b','integer_t',native)
    helpers=re.sub(r'\bint\b','integer_t',helpers)
    oracle='module oracle(input logic[7:0] data,input logic enable,output logic[127:0] result);typedef bit signed[31:0] integer_t;'+helpers+'function automatic logic[127:0] calculate(input logic[7:0] data,input logic enable);'+native+'endfunction\nalways_comb result=calculate(data,enable);endmodule\n'
    bench='''module tb;reg[7:0] data;reg enable;wire[127:0] result,expected;integer trial,k;
local_auto dut(.*);oracle reference(.data(data),.enable(enable),.result(expected));
initial begin for(trial=0;trial<1024;trial=trial+1) begin
 data=8'(trial*31+7);enable=trial[0];
 if(trial%7==0) data[trial%8]=1'bx;
 if(trial%11==0) data[trial%8]=1'bz;
 #1;if(result!==expected) $fatal(1,"automatic locals trial=%0d got=%h expected=%h",trial,result,expected);
end $display("PASS automatic locals: 1024 native automatic-function comparisons, entry snapshots, defaults, records, arrays, loops and X/Z");$finish;end endmodule
'''
    with tempfile.TemporaryDirectory(prefix='pigen-automatic-local-') as temporary:
        root=Path(temporary)
        pigen,sv,reference,tb,sim=[root/name for name in ('auto.pigen','auto.sv','oracle.sv','tb.sv','sim')]
        pigen.write_text(source);reference.write_text(oracle);tb.write_text(bench)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root)
        run(['verilator','--lint-only','--top-module','local_auto',sv],root)
        run(['iverilog','-g2012','-s','tb','-o',sim,sv,reference,tb],root)
        print(run(['vvp',sim],root).strip())
        for body in (
            'automatic logic[7:0] wire net;',
            'automatic uint[8] buf storage;',
            'automatic localparam A=1;',
            'automatic logic[7:0] value=missing;',
            'automatic logic[7:0] value=token;',
            'automatic logic[7:0] value=data;static logic[7:0] captured=value;',
            'automatic logic[7:0] value=data;static logic[7:0] captured=peek(value);',
            'begin : named automatic logic[7:0] value;result=named.value;end',
            'begin : named automatic logic[7:0] value;end result=named.value;',
            'begin automatic logic[7:0] value;end result=value;',
        ):
            pigen.write_text('module bad(input logic[7:0] data,input logic[7:0] buf token,output logic[7:0] result);always_comb begin uint[8] marker;'+body+'marker=0;result=0;end endmodule')
            sv.write_text('preserved output\n')
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root,expect=False)
            assert sv.read_text()=='preserved output\n'
        print('PASS automatic lifetime errors preserve output: nets, storage, constants, names, buffered reads, static capture and hierarchical escape')


if __name__=='__main__':
    main()
