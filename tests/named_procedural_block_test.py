#!/usr/bin/env python3
"""Named typed combinational scopes preserve lexical and ordered SV behavior."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    source = """module named_blocks(input logic[7:0] data,input logic enable,output logic[31:0] result);
always_comb begin : observe
 uint[8] marker;
 logic[7:0] prior_value;
 marker=0;prior_value=data;
 begin : sample
  localparam logic[7:0] MASK=8'h5a;
  static logic[7:0] seed=MASK;
  logic[7:0] value;
  value=prior_value^sample.seed;
  begin : nested
   logic[7:0] value;
   value=sample.value+8'h1;
   result[7:0]=value;
  end : nested
  result[15:8]=nested.value;
 end : sample
 result[23:16]=sample.value;
 result[31:24]=8'($bits(sample.value))^sample.MASK;
 if(enable) begin : conditional
  result[31:24]=sample.value^8'hff;
 end : conditional
end : observe
endmodule
"""
    oracle = source.replace('module named_blocks(', 'module oracle(').replace('uint[8] marker;', 'logic[7:0] marker;')
    bench = """module tb;reg[7:0] data;reg enable;wire[31:0] result,expected;integer trial;
named_blocks dut(.*);oracle ref_dut(.data(data),.enable(enable),.result(expected));
initial begin for(trial=0;trial<1024;trial=trial+1) begin
 data=8'(trial*37+trial/7);enable=trial[0];#1;
 if(result!==expected) $fatal(1,"named procedural scope trial=%0d actual=%h expected=%h",trial,result,expected);
end $display("PASS named procedural blocks: 1024 native SV comparisons, nested names, shadowing, queries and ordered locals");$finish;end endmodule
"""
    with tempfile.TemporaryDirectory(prefix='pigen-named-block-') as temporary:
        directory=Path(temporary)
        pigen,sv,native,tb,sim=[directory/name for name in ('named.pigen','named.sv','oracle.sv','tb.sv','sim')]
        pigen.write_text(source);native.write_text(oracle);tb.write_text(bench)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
        run(['verilator','--lint-only','--top-module','named_blocks',sv],directory)
        run(['iverilog','-g2012','-s','tb','-o',sim,sv,native,tb],directory)
        print(run(['vvp',sim],directory).strip())
        failures=(
            'typedef struct packed {logic[7:0] value;} record_t; record_t future; begin result=future.value; begin : future logic[7:0] value;value=0;end end',
            'begin : ; end',
            'begin : one end : two',
            'begin end : unnamed',
            'begin : duplicate end begin : duplicate end',
            'logic[7:0] duplicate; begin : duplicate end',
            'begin : scope logic[7:0] private_value;private_value=0;end result=scope.missing;',
            'begin : scope logic[7:0] private_value;private_value=0;end result=private_value;',
            'begin : scope logic[7:0] private_value;private_value=0;end result=scope[0].private_value;',
        )
        for statement in failures:
            pigen.write_text('module invalid(output logic[31:0] result);always_comb begin uint[8] marker;'+statement+' marker=0;result=0;end endmodule')
            sv.write_text('preserved output\n')
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory,expect=False)
            assert sv.read_text()=='preserved output\n'
        for selected in (0,1):
            for reversed_order in (False,True):
                generated=f'if({selected}) begin : bank uint[32] axi4_lite bus;end '
                procedural='always_comb begin : bank uint[8] marker;marker=0;result=0;end '
                body=procedural+generated if reversed_order else generated+procedural
                pigen.write_text('module conflict(input wire clk,input wire reset,output logic[7:0] result);'+body+'endmodule')
                sv.write_text('preserved output\n')
                run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory,expect=False)
                assert sv.read_text()=='preserved output\n'
        print('PASS named procedural scope diagnostics preserve output')


if __name__=='__main__':
    main()
