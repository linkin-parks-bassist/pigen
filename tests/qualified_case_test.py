#!/usr/bin/env python3
"""Native qualifier checking survives typed cases and comma-item grouping."""
from pathlib import Path
import re
import subprocess
import tempfile
from native_protocol_test import ROOT, run


def compile_case(directory, qualifier, body, keyword="case"):
    source, emitted = directory/'case.pigen', directory/'case.sv'
    source.write_text(f'''module qualified(input logic [7:0] data,input logic [7:0] a,input logic [7:0] b,output logic [7:0] q);
always_comb begin
 uint[8] selector; selector=uint[8]'(data); q=0;
 {qualifier} {keyword}(selector) {body} endcase
end
endmodule
''')
    run([ROOT/'pigen', source, '-o', emitted, '--no-diagram'], directory)
    assert f'{qualifier} {keyword} (' in emitted.read_text()
    return emitted


def simulate(directory, emitted, tb_text, label):
    tb=directory/'tb.sv'; tb.write_text(tb_text)
    objects=directory/label
    run(['verilator','--binary','--assert','--timing','-j','2','--top-module','tb',
         '--Mdir',objects,'-o','sim',emitted,tb],directory)
    return objects/'sim'


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-qualified-case-') as temporary:
        root=Path(temporary)
        for qualifier in ('unique','unique0','priority'):
            directory=root/qualifier;directory.mkdir()
            # Dynamic comma labels may both match: this remains ONE item.
            emitted=compile_case(directory,qualifier,'a,b: begin selector=0; q=8\'h31; end default: q=8\'h72;')
            binary=simulate(directory,emitted,'''module tb;
reg [7:0] data,a,b; wire [7:0] q; integer i;
qualified dut(.*);
initial begin
 for(i=0;i<256;i=i+1) begin
  data=8'(i); a=data; b=8'(i+1); #1;
  if(q!==8'h31) $fatal(1,"group result");
  a=8'(i+1); b=a; #1; if(q!==8'h72) $fatal(1,"default result");
 end
 if($test$plusargs("equal")) begin data=0; a=0; b=0; #1; end
 $display("PASS grouped qualifier");$finish;
end
endmodule
''','group')
            assert 'PASS grouped qualifier' in run([binary],directory)
            # Verilator 5.020 falsely counts equal expressions within ONE item
            # as multiple matches. Preserve native syntax and witness parity;
            # this expected simulator failure is not a language restriction.
            native_group=directory/'native_group.sv'
            native_group.write_text(f"""module qualified(input logic [7:0] data,input logic [7:0] a,input logic [7:0] b,output logic [7:0] q);
always_comb begin
 logic [7:0] selector; selector=data; q=0;
 {qualifier} case(selector) a,b: begin selector=0; q=8'h31; end default: q=8'h72; endcase
end
endmodule
""")
            native_binary=simulate(directory,native_group,(directory/'tb.sv').read_text(),'native_group')
            assert 'PASS grouped qualifier' in run([native_binary],directory)
            native_result=subprocess.run([native_binary,'+equal'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            with (directory/'commands.log').open('a') as log: log.write(f'$ {native_binary} +equal\n'+native_result.stdout)
            equal=run([binary,'+equal'],directory,expect=native_result.returncode==0)
            if native_result.returncode: assert 'multiple matches' in equal and 'multiple matches' in native_result.stdout

            # Separate authored items must keep the qualifier's overlap checks.
            emitted=compile_case(directory,qualifier,"a: q=8'h31; b: q=8'h42; default: q=8'h72;")
            binary=simulate(directory,emitted,'''module tb;
reg [7:0] data=1,a=1,b=2; wire [7:0] q;
qualified dut(.*);
initial begin
 #1; if(q!==8'h31) $fatal(1,"first item");
 data=2; #1; if(q!==8'h42) $fatal(1,"second item");
 data=3; #1; if(q!==8'h72) $fatal(1,"default item");
 data=1; b=1; #1;
 if(q!==8'h31) $fatal(1,"priority order");
 $display("PASS overlap allowed");$finish;
end
endmodule
''','separate')
            result=run([binary],directory,expect=qualifier=='priority')
            if qualifier!='priority': assert 'multiple matches' in result, result
            # Exact authored default-only/empty/missing-default structure.
            for body,defaults in [("default: q=8'h33;",1),("",0),("a: q=8'h31;",0)]:
                emitted=compile_case(directory,qualifier,body)
                text=emitted.read_text();case=text.split(f'{qualifier} case (',1)[1].split('endcase',1)[0]
                assert case.count('default:')==defaults and "'d0: begin" not in case
                # Compare diagnostics to independently authored native syntax;
                # old Verilator versions warn even for incomplete unique0.
                native=directory/'native.sv'
                native.write_text(f'''module native_case(input logic [7:0] data,input logic [7:0] a,output logic [7:0] q);
always_comb begin q=0; {qualifier} case(data) {body} endcase end
endmodule
''')
                outcomes=[]
                for path,top in [(emitted,'qualified'),(native,'native_case')]:
                    result=subprocess.run(['verilator','--lint-only','--assert','--top-module',top,str(path)],text=True,capture_output=True)
                    outcomes.append((result.returncode==0,set(re.findall(r'%Warning-([A-Z0-9_]+)',result.stderr))))
                assert outcomes[0]==outcomes[1],outcomes
            for keyword in ('casez','casex'):
                emitted=compile_case(directory,qualifier,"8'b???????1: q=8'h31; default: q=8'h72;",keyword)
                if keyword=='casez':
                    run(['verilator','--lint-only','--assert','--top-module','qualified',emitted],directory)
                else:
                    diagnostics=run(['verilator','--lint-only','--assert','--top-module','qualified',emitted],directory,expect=False)
                    assert 'CASEX' in diagnostics
            print(f'PASS {qualifier}: 256 comma groups and equal-label native parity, separate-item checks, priority/default behavior and native lint parity')


if __name__=='__main__':
    main()
