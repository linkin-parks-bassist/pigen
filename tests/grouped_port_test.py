#!/usr/bin/env python3
"""ANSI inheritance must reach typed compilation and preserve each port shape."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-grouped-ports-') as temporary:
        root=Path(temporary)
        for mode in ('ordinary','numeric','parameterized','signed','instance8','instance12'):
            directory=root/mode;directory.mkdir()
            width=12 if mode in ('parameterized','instance12') else 8
            parameter='#(parameter W=12)' if mode=='parameterized' or mode.startswith('instance') else ''
            size='W' if parameter else '8'
            data_type=f'int[{size}] wire' if mode=='signed' else f'uint[{size}] wire' if mode=='numeric' else f'logic [{size}-1:0]'
            expression="(a[0] >>> 2) + a[1] + b[0] + b[1] + data" if mode=='signed' else 'a[0] + a[1] + b[0] + b[1] + data'
            source=directory/'ports.pigen';emitted=directory/'ports.sv';tb=directory/'tb.sv'
            source.write_text(f'''module grouped {parameter}(input {data_type} data,a[0:1],b[1:0],output {data_type} result,mirrored);
always_comb begin
 uint[{size}] marker; marker=0;
 result={expression}; mirrored=result ^ {size}'(8'h5a);
end
endmodule
''')
            top='grouped'
            if mode.startswith('instance'):
                top='instantiated'
                source.write_text(source.read_text()+f"""module instantiated(input logic [{width}-1:0] data,a[0:1],b[1:0],output logic [{width}-1:0] result,mirrored);
 grouped #(.W({width})) configured(.data(data),.a(a),.b(b),.result(result),.mirrored(mirrored));
endmodule
""")
            run([ROOT/'pigen',source,'-o',emitted,'--no-diagram'],directory)
            text=emitted.read_text();assert 'uint[' not in text and 'int[' not in text
            sign='signed' if mode=='signed' else ''
            variable='bit' if mode in ('numeric','signed') else 'reg'
            tb.write_text(f'''module tb;
{variable} {sign} [{width}-1:0] data,a[0:1],b[1:0]; wire {sign} [{width}-1:0] result,mirrored;
reg [{width}-1:0] expected; integer i;
{top} dut(.*);
initial begin
 for(i=0;i<1024;i=i+1) begin
  data={width}'(i*17); a[0]={width}'(i*31); a[1]={width}'(i*47); b[0]={width}'(i*67); b[1]={width}'(i*89);
  expected={expression}; #1;
  if(result!==expected || mirrored!==(expected ^ {width}'h5a)) $fatal(1,"inherited port type/shape mode={mode} trial=%0d result=%h expected=%h",i,result,expected);
 end
 $display("PASS grouped ports {mode}: 1024 values, inherited type/direction and independent shapes");$finish;
end
endmodule
''')
            binary=directory/'sim'
            run(['iverilog','-g2012','-s','tb','-o',binary,emitted,tb],directory)
            assert f'PASS grouped ports {mode}' in run(['vvp',binary],directory)
            run(['verilator','--lint-only','--timing','--top-module','tb',emitted,tb],directory)
            print(f'PASS grouped ports {mode}: 1024 values and fatal lint')
        for parameter in ('', '#(parameter W=8)'):
            source=root/'bad.pigen';emitted=root/'bad.sv';emitted.write_text('preserved output')
            source.write_text(f'module bad {parameter}(input logic [7:0] a,b); always_comb begin uint[0] marker; marker=0; end endmodule')
            diagnostics=run([ROOT/'pigen',source,'-o',emitted,'--no-diagram'],root,expect=False)
            assert 'positive integer' in diagnostics and emitted.read_text()=='preserved output'
        source=root/'ordinary.sv';emitted=root/'ordinary_out.sv'
        original='module ordinary(a,b); input [7:0] a; output [7:0] b; assign b=a; endmodule\n'
        source.write_text(original)
        run([ROOT/'pigen',source,'-o',emitted,'--no-diagram'],root)
        assert emitted.read_text().strip()==original.strip()
        run(['verilator','--lint-only','--top-module','ordinary',emitted],root)
        print('PASS grouped port diagnostics preserve output; genuine non-ANSI module retains ordinary SV')



if __name__=='__main__':
    main()
