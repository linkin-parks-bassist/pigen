#!/usr/bin/env python3
"""Authored comma items survive typing/lowering and ordered body writes."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-case-group-') as temporary:
        directory = Path(temporary)
        source, output, tb = (directory/name for name in ('group.pigen', 'group.sv', 'tb.sv'))
        source.write_text('''module grouped(input logic [7:0] data,a,b,output logic [7:0] result);
always_comb begin
 uint[8] selector;
 selector=uint[8]'(data); result=8'h55;
 case(selector)
 a,b: begin selector=0; result=8'h21;
   casez(data) 8'b???????1,8'b??????10: result=8'h43; default: ; endcase
 end
 default: result=8'h65;
 endcase
 result=result ^ 8'(selector);
end
endmodule
''')
        run([ROOT/'pigen', source, '-o', output, '--no-diagram'], directory)
        emitted = output.read_text()
        # Both authored groups must remain comma lists, with a single body each.
        assert sum(', ' in line and ': begin' in line for line in emitted.splitlines()) == 2, emitted
        tb.write_text('''module tb;
reg [7:0] data,a,b; wire [7:0] result; reg [7:0] expected; integer i,j;
grouped dut(.*);
initial begin
 for(i=0;i<256;i=i+1) for(j=0;j<3;j=j+1) begin
  data=8'(i); a=j==0 ? data : 8'(i+1); b=j==1 ? data : a;
  if(data==a || data==b) expected=(data[0] || data[1:0]==2'b10) ? 8'h43 : 8'h21;
  else expected=8'h65 ^ data;
  #1; if(result!==expected) $fatal(1,"group data=%h a=%h b=%h result=%h expected=%h",data,a,b,result,expected);
 end
 $display("PASS comma case groups: 768 dynamic/equal labels and branch-entry selector writes"); $finish;
end
endmodule
''')
        executable = directory/'sim'
        run(['iverilog', '-g2012', '-s', 'tb', '-o', executable, output, tb], directory)
        print(run(['vvp', executable], directory).strip())
        run(['verilator', '--lint-only', '--timing', '--top-module', 'tb', output, tb], directory)


if __name__ == '__main__':
    main()
