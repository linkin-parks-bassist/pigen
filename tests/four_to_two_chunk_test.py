#!/usr/bin/env python3
"""Nested two-state conversions match native SV at integer chunk boundaries."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    widths = (1, 7, 8, 16, 31, 32, 33, 63, 64, 65, 127, 128, 129, 257)
    ports, reference_ports, statements, reference, names = [], [], [], [], []
    for width in widths:
        for prefix, dtype, signed in (('u', 'bit', ''), ('s', 'int', 'signed '), ('n', 'bit', ''), ('z', 'bit', '')):
            name = f'{prefix}{width}'
            names.append((name, width))
            ports.append(f'output {dtype}[{width}] wire {name}')
            reference_ports.append(f'output bit {signed}[{width-1}:0] {name}')
        reference.append(f'typedef bit[{width-1}:0] u_t{width}; typedef bit signed[{width-1}:0] s_t{width}; typedef bit[{width}:0] w_t{width};')
        statements += [f'assign u{width}=bit[{width}]\'(raw);',
                       f'assign s{width}=int[{width}]\'(signed_raw);',
                       f'assign n{width}=bit[{width}]\'(bit[{width+1}]\'(raw)^bit[{width+1}]\'(raw>>1));']
        statements.append(f"assign z{width}=bit[{width}]'('z);")
        reference.append(f"assign z{width}=u_t{width}'('z);")
        reference += [f'assign u{width}=u_t{width}\'(raw);',
                      f'assign s{width}=s_t{width}\'(signed_raw);',
                      f'assign n{width}=u_t{width}\'(w_t{width}\'(raw)^w_t{width}\'(raw>>1));']
    ports.append('output bit[65] wire child_value')
    reference_ports.append('output bit[64:0] child_value')
    names.append(('child_value',65))
    statements.append('cast_leaf #(.W(65)) child(.raw(raw),.value(child_value));')
    reference.append("assign child_value=u_t65'(raw);")
    inputs = 'input logic[256:0] raw,input logic signed[32:0] signed_raw,'
    source = 'module conversion('+inputs+','.join(ports)+');\n'+'\n'.join(statements)+'\nendmodule\n'
    source = "module cast_leaf #(parameter W=33)(input logic[256:0] raw,output bit[W] wire value);always_comb begin value=bit[W]'(raw);end endmodule\n"+source
    oracle = 'module oracle('+inputs+','.join(reference_ports)+');\n'+'\n'.join(reference)+'\nendmodule\n'
    declarations = ''.join(f'wire[{width-1}:0] {name},expected_{name};\n' for name, width in names)
    actual = ','.join(name for name, _ in names)
    expected = ','.join('expected_'+name for name, _ in names)
    connections = ','.join(f'.{name}(expected_{name})' for name, _ in names)
    bench = '''module tb;reg[256:0] raw;reg signed[32:0] signed_raw;integer trial,k;
''' + declarations + '''conversion dut(.*);
oracle ref_dut(.raw(raw),.signed_raw(signed_raw),''' + connections + ''');
initial begin
 for(trial=0;trial<256;trial=trial+1) begin
  for(k=0;k<257;k=k+1) begin
   case((trial*37+k*13)%8)
    0:raw[k]=1'bx; 1:raw[k]=1'bz; 2,3:raw[k]=0; default:raw[k]=1;
   endcase
  end
  signed_raw=33'(raw);#1;
  if({''' + actual + '''}!=={''' + expected + '''}) $fatal(1,"two-state conversion trial=%0d",trial);
 end
 $display("PASS two-state conversion chunks: 256 X/Z patterns, 14 widths, signed extension and nested casts");$finish;
end endmodule
'''
    with tempfile.TemporaryDirectory(prefix='pigen-two-state-chunks-') as temporary:
        directory = Path(temporary)
        pigen, sv, native, tb, sim = [directory/name for name in ('convert.pigen', 'convert.sv', 'oracle.sv', 'tb.sv', 'sim')]
        pigen.write_text(source); native.write_text(oracle); tb.write_text(bench)
        run([ROOT/'pigen', pigen, '-o', sv, '--no-diagram'], directory)
        run(['verilator', '--lint-only', '--top-module', 'conversion', sv], directory)
        run(['iverilog', '-g2012', '-s', 'tb', '-o', sim, sv, native, tb], directory)
        print(run(['vvp', sim], directory).strip())


if __name__ == '__main__':
    main()
