#!/usr/bin/env python3
"""Typed foreach behavior against native and independent explicit traversal."""
from pathlib import Path
import subprocess
import tempfile
from native_protocol_test import ROOT, run


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-foreach-') as temporary:
        directory = Path(temporary)
        sv, sim = directory/'foreach.sv', directory/'sim'
        run([ROOT/'pigen', ROOT/'tests/foreach.pigen', '-o', sv, '--no-diagram'], directory)
        run(['verilator', '--lint-only', '--top-module', 'foreach_core', sv, ROOT/'rtl/pigen_primitives.sv'], directory)
        run(['iverilog', '-g2012', '-s', 'foreach_tb', '-o', sim, sv, ROOT/'tests/foreach_tb.sv', ROOT/'rtl/pigen_primitives.sv'], directory)
        output = run(['vvp', sim], directory)
        assert 'PASS foreach:' in output
        print(output.strip())
        run([ROOT/'pigen', ROOT/'examples/native_axi_generated.pigen', '-o', sv, '--no-diagram'], directory)
        run(['verilator', '--lint-only', '--top-module', 'native_axis_foreach_data', sv, ROOT/'rtl/pigen_primitives.sv'], directory)
        run(['verilator', '--lint-only', '--top-module', 'native_axis_foreach_bank', sv, ROOT/'rtl/pigen_primitives.sv'], directory)
        parameter_source=directory/'parameterized.pigen'
        parameter_sv=directory/'parameterized.sv'
        parameter_source.write_text((ROOT/'examples/native_axi_generated.pigen').read_text()+"""
module parameterized_foreach(input wire clk,input wire reset,
 input uint[32] axi4_stream fifo[3] s2[2],input uint[32] axi4_stream fifo[3] s3[3],
 output uint[32] wire q2,output uint[32] wire q3,output bit[2] wire v2,output bit[3] wire v3);
 native_axis_foreach_bank #(.LANES(2)) two(.clk(clk),.reset(reset),.s(s2),.checksum(q2),.offered(v2));
 native_axis_foreach_bank #(.LANES(3)) three(.clk(clk),.reset(reset),.s(s3),.checksum(q3),.offered(v3));
endmodule
""")
        run([ROOT/'pigen', parameter_source, '-o', parameter_sv, '--no-diagram'], directory)
        run(['verilator', '--lint-only', '--top-module', 'parameterized_foreach', parameter_sv, ROOT/'rtl/pigen_primitives.sv'], directory)
        print('PASS native foreach example: independent two/three-lane parameter overrides and complete child contracts')
        tb=directory/'observe_tb.sv'
        tb.write_text("""module observe_tb;
reg clk=0,reset=0,s_tvalid=0; reg [31:0] s_tdata; wire s_tready; wire [31:0] reversed;
reg [31:0] expected; integer trial,bit_index;
native_axis_foreach_data dut(.*);
initial begin
 for(trial=0;trial<1024;trial=trial+1) begin
  reset=1; s_tvalid=0; clk=0; #1; clk=1; #1; clk=0;
  s_tdata=32'(trial*78912345); reset=0; s_tvalid=1;
  for(bit_index=0;bit_index<32;bit_index=bit_index+1) expected[31-bit_index]=s_tdata[bit_index];
  #1; clk=1; #1; clk=0; s_tvalid=0; #1;
  if(reversed!==expected || s_tready!==1'b0) $fatal(1,"foreach AXI field observation input=%h got=%h expected=%h ready=%b trial=%0d",s_tdata,reversed,expected,s_tready,trial);
  s_tdata=~s_tdata; #1;
  if(reversed!==expected || s_tready!==1'b0) $fatal(1,"foreach peek drained or bypassed buffered input");
 end
 $display("PASS foreach AXI field example: 1024 reversed words, validity/reset and no token consumption"); $finish;
end
endmodule
""")
        objects=directory/'observe_obj'
        run(['verilator', '--binary', '--timing', '-j', '2', '--top-module', 'observe_tb', '--Mdir', objects, '-o', 'observe_sim', sv, tb, ROOT/'rtl/pigen_primitives.sv'], directory)
        output=run([objects/'observe_sim'], directory)
        assert 'PASS foreach AXI field example:' in output
        print(output.strip())
        header = 'module invalid(input logic [7:0] data,output logic [7:0] q); always_comb begin uint[8] mark; logic [7:0] cells[-1:1][3:2]; mark=0; q=0; '
        failures = {
            'write': 'foreach(cells[i,j]) i=0;',
            'increment': 'foreach(cells[i,j]) j++;',
            'slice_write': 'foreach(cells[i,j]) i[0]=0;',
            'concat_write': 'foreach(cells[i,j]) {q,i}=0;',
            'duplicate': 'foreach(cells[i,i]) q=8\'(i);',
            'array_name': 'foreach(cells[cells]) q=0;',
            'excess_rank': 'foreach(cells[i,j,k,l]) q=0;',
            'scope_escape': 'foreach(cells[i,j]) q=8\'(i+j); q=8\'(i);',
            'malformed': 'foreach(cells[i+1]) q=0;',
            'missing_target': 'foreach(missing[i]) q=0;',
            'constant_target': "foreach(8'hff[i]) q=0;",
            'empty_indices': 'foreach(cells[]) q=0;',
            'clocked_write': 'foreach(cells[i,j]) q<=0;',
        }
        for name, statement in failures.items():
            source=directory/(name+'.pigen');source.write_text(header+statement+' end endmodule')
            sv.write_text('preserved output\n')
            result=subprocess.run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],cwd=directory,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            assert result.returncode and sv.read_text()=='preserved output\n', (name,result.stdout)
        print('PASS foreach negatives: read-only indices, scope, rank, targets, syntax and output preservation')


if __name__ == '__main__':
    main()
