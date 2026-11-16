#!/usr/bin/env python3
"""Numerical-only generate regions and specialized children match native SV."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def leaf(family, explicit, casts, oracle=False):
    dtype='payload_t' if oracle else f'{family}[W]'
    declaration='wire payload_t value;' if oracle else f'{dtype} wire value;'
    value=f"{dtype}'(data)" if casts else "'1"
    def body(index, invert=False):
        expression='~value' if invert else 'value'
        if casts:
            expression=('~' if invert else '')+value
            return f"assign result[{index}]=W'({expression});"
        return declaration+f"assign value={value};assign result[{index}]=W'({expression});"
    items=f'''if(W>0) begin : choice {body(0)} end else begin : dead
 {('wire payload_t poison;' if oracle else family+'[999999999] wire poison;')}
 assign poison=missing;
end
for(genvar g=0;g<2;g++) begin : lanes
 if(g==0) begin : pick {body('g+1')} end else begin : pick {body('g+1',True)} end
end
case(W)
 8: begin : selected {body(3)} end
 12: begin : selected {body(3,True)} end
 default: begin : selected assign result[3]='0;end
endcase
{('if(1) ' if oracle else '')}begin : plain {body(4)} end
if(1) assign result[5]=W'(data);
'''
    typedef=f"typedef bit {'signed ' if family=='int' else ''}[W-1:0] payload_t;\n" if oracle else ''
    name='oracle' if oracle else 'numerical_leaf'
    region='generate\n'+items+'endgenerate\n' if explicit else items
    return f'module {name} #(parameter W=8)(input logic[W-1:0] data,output logic[W-1:0] result[0:5]);\n'+typedef+region+'endmodule\n'


def profile(directory, family, explicit, casts):
    source=leaf(family,explicit,casts)+'''module numerical_top(input logic[11:0] data,output logic[7:0] narrow_result[0:5],output logic[11:0] wide_result[0:5]);
numerical_leaf #(.W(8)) first(.data(data[7:0]),.result(narrow_result));
numerical_leaf #(.W(12)) second(.data(data),.result(wide_result));endmodule
'''
    bench='''module tb;reg[11:0] data;wire[7:0] narrow_result[0:5],expected_small[0:5];wire[11:0] wide_result[0:5],expected_large[0:5];integer trial,k;
numerical_top dut(.*);
oracle #(.W(8)) first(.data(data[7:0]),.result(expected_small));
oracle #(.W(12)) second(.data(data),.result(expected_large));
initial begin for(trial=0;trial<512;trial=trial+1) begin
 for(k=0;k<12;k=k+1) begin
  case((trial+k*7)%8) 0:data[k]=1'bx;1:data[k]=1'bz;2,3:data[k]=0;default:data[k]=1;endcase
 end
 #1;for(k=0;k<6;k=k+1) begin
  if(narrow_result[k]!==expected_small[k] || wide_result[k]!==expected_large[k]) $fatal(1,"numerical generate trial=%0d slot=%0d",trial,k);
 end
end $display("PASS numerical generate: 512 native SV comparisons, independent 8/12-bit overrides, X/Z and all control forms");$finish;end endmodule
'''
    pigen,sv,native,tb,sim=[directory/name for name in ('generate.pigen','generate.sv','oracle.sv','tb.sv','sim')]
    pigen.write_text(source);native.write_text(leaf(family,explicit,casts,True));tb.write_text(bench)
    run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','numerical_top',sv],directory)
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,native,tb],directory)
    print(f'{family} explicit={explicit} casts={casts}: '+run(['vvp',sim],directory).strip())


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-numerical-generate-') as temporary:
        root=Path(temporary)
        for family in ('uint','int','bit'):
            for explicit in (False,True):
                for casts in (False,True):
                    directory=root/f'{family}_{explicit}_{casts}';directory.mkdir()
                    profile(directory,family,explicit,casts)
        pigen,sv=root/'ordinary.pigen',root/'ordinary.sv'
        ordinary='''module ordinary #(parameter W=8)(input logic[W-1:0] uint,output wire[W-1:0] result);
generate if(W>0) begin : keep_sv
 bit[W-1:0] saved;
 always_comb begin saved=uint;if(uint[0] inside {1'b1}) saved[0]=uint[0];end
 assign result={saved[W-1:1],uint[0]};
end endgenerate endmodule
'''
        pigen.write_text(ordinary)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root)
        assert sv.read_text()==ordinary
        run(['verilator','--lint-only','--top-module','ordinary',sv],root)
        for body in ('uint[0] wire bad;', 'uint[] wire bad;', "assign result=uint[0]'(data);"):
            pigen.write_text('module invalid(input logic[7:0] data,output logic[7:0] result);if(1) begin '+body+' end endmodule')
            sv.write_text('preserved output\n')
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root,expect=False)
            assert sv.read_text()=='preserved output\n'
        print('PASS numerical generate selection: ordinary packed ranges/indexed values preserved; invalid numerical constructs preserve output')


if __name__=='__main__':
    main()
