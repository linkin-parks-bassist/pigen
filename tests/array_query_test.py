#!/usr/bin/env python3
"""Compare fixed array inquiries with native SV, including unknown dimensions."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    queries = [
        '$dimensions(cells)', '$unpacked_dimensions(cells)',
        '$left(cells)', '$right(cells)', '$low(cells)', '$high(cells)',
        '$increment(cells)', '$size(cells)', '$left(cells,2)', '$right(cells,2)',
        '$size(cells,2)', '$increment(cells,2)', '$left(cells,3)', '$right(cells,3)',
        '$size(cells,3)', '$left(cells,4)', '$right(cells,4)', '$size(cells,4)',
        '$size(cells,0)', '$size(cells,5)', "$size(cells,'x)",
        '$dimensions(cells[index])', '$unpacked_dimensions(cells[index])',
        '$size(cells[index],2)', '$dimensions(scalar)', '$size(scalar)',
        '$dimensions(record)', '$left(record)', '$right(record)',
        '$dimensions(atom)', '$left(atom)', '$right(atom)',
    ]
    types = '''typedef logic [3:0][2:1] word_t;
typedef struct packed {logic a;} record_t;
'''
    declarations = '''word_t cells[-2:1][7:5];
logic scalar;
record_t record;
integer atom;
'''
    ports = ['input logic signed [2:0] index'] + [
        f'output logic [31:0] q{i}' for i in range(len(queries))]
    assignments = '\n'.join(f'q{i}={query};' for i,query in enumerate(queries))
    # A Pigen local forces structured expression analysis for the whole process.
    pigen = 'module inquiry('+','.join(ports)+');\n'+types+'always_comb begin\n' + declarations
    pigen += 'uint[8] marker; marker=0;\n'+assignments+'\nend\nendmodule\n'
    oracle = 'module oracle('+','.join(ports)+');\n'+types+declarations
    # Icarus cannot elaborate a partially selected unpacked array argument.
    # Its remaining type is represented explicitly for the independent oracle.
    oracle += 'word_t selected_row[7:5];\nalways_comb begin\n'+assignments.replace('cells[index]','selected_row')+'\nend\nendmodule\n'
    tb = 'module tb; reg signed [2:0] index; integer i;\n'
    tb += '\n'.join(f'wire [31:0] actual{i},expected{i};' for i in range(len(queries)))
    for module,prefix in [('inquiry','actual'),('oracle','expected')]:
        tb += module+' dut_'+prefix+'(.index(index),'+','.join(
            f'.q{i}({prefix}{i})' for i in range(len(queries)))+');\n'
    checks = '\n'.join(f'if(actual{i}!==expected{i}) $fatal(1,"query {i} mismatch got=%h expected=%h",actual{i},expected{i});' for i in range(len(queries)))
    tb += 'initial begin for(i=0;i<32;i++) begin index=3\'(i); #1;\n'+checks+'\nend\n'
    tb += f'$display("PASS fixed array inquiries: {len(queries)} native SV comparisons, signed runtime indices and X dimensions"); $finish; end endmodule\n'
    with tempfile.TemporaryDirectory(prefix='pigen-array-query-') as temporary:
        root=Path(temporary)
        source,sv,reference,bench,binary=[root/name for name in ('query.pigen','query.sv','oracle.sv','tb.sv','sim')]
        source.write_text(pigen);reference.write_text(oracle);bench.write_text(tb)
        run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],root)
        # Verilator 5.020 internally fails on the oracle's X dimension argument.
        # Lint compiler output fatally; execute the four-state oracle in Icarus.
        run(['verilator','--lint-only','--top-module','inquiry',sv],root)
        run(['iverilog','-g2012','-s','tb','-o',binary,sv,reference,bench],root)
        print(run(['vvp',binary],root).strip())


if __name__=='__main__':
    main()
