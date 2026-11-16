#!/usr/bin/env python3
"""Independent native pin tests for runtime field coordinates and packed DATA."""
from pathlib import Path
import subprocess
from native_protocol_test import ROOT


def run(args, directory):
    result = subprocess.run(list(map(str, args)), cwd=directory, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    with (directory / 'commands.log').open('a') as log:
        log.write('$ ' + ' '.join(map(str, args)) + '\n' + result.stdout)
    assert result.returncode == 0, result.stdout
    return result.stdout


def case(family, datatype, child=False):
    packed = datatype == 'bit[4][8]'
    directory = ROOT / 'build/native-projections' / (family + '-' + datatype.replace('[', '_').replace(']', '') + ('-child' if child else ''))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'commands.log').write_text('')
    endpoint = 'child.m' if child else 's'
    field = endpoint + ('.data' if family == 'axi4_stream' else '.w.data')
    if packed:
        lane, bit, low = f'{field}[0][1:0]', f'{field}[0][4:2]', f"uint[3]'({field}[0][6:5])"
        expression = f"{{{field}[{lane}][{bit}],{field}[{lane}][{low} +: 4],{field}[{lane}][({low}+3) -: 4]}}"
        bits = 9
        reference = "{1'(v >> (((v&3)*8)+((v>>2)&7))),4'(v >> (((v&3)*8)+((v>>5)&3))),4'(v >> (((v&3)*8)+((v>>5)&3)))}"
    else:
        expression = f"{{{field}[{field}[4:0]],{field}[uint[5]'({field}[3:0]) +: 8],{field}[(uint[5]'({field}[3:0])+7) -: 8]}}"
        bits = 17
        reference = "{1'(v >> (v&31)),8'(v >> (v&15)),8'(v >> (v&15))}"
    aux = endpoint + ('.keep' if family == 'axi4_stream' else '.w.strb')
    coordinate = f'{field}[0][1:0]' if packed else f'{field}[1:0]'
    expression = expression[:-1] + f",{aux}[uint[2]'({coordinate})]" + '}'
    reference = reference[:-1] + ",1'(v >> (4+(v&3)))}"
    bits += 1
    protocol = family + ('[keep=1]' if family == 'axi4_stream' else '')
    source, sv, tb, sim = [directory / name for name in ('fields.pigen', 'fields.sv', 'tb.sv', 'sim')]
    design = ''
    if child:
        design = f'module field_source(input wire clk,input wire reset,input {datatype} {protocol} s,output {datatype} {protocol} m);\nalways @(posedge clk) if(!reset) begin\n'
        for channel in (('beat',) if family == 'axi4_stream' else ('aw', 'w', 'b', 'ar', 'r')):
            dst, src = ('s', 'm') if channel in ('b', 'r') else ('m', 's')
            design += f'{dst}.{channel} <= {src}.{channel};\n'
        design += 'end\nendmodule\n'
    design += f'module native_fields(input wire clk,input wire reset,input {datatype} {protocol} s,output bit[{bits}] buf result);\n'
    if child: design += 'field_source child(.clk(clk),.reset(reset),.s(s));\n'
    source.write_text(design + f'always @(posedge clk) if(!reset) result <= {expression};\nendmodule\n')
    run([ROOT / 'pigen', source, '-o', sv, '--no-diagram'], directory)
    run(['verilator', '--lint-only', '--top-module', 'native_fields', sv, ROOT / 'rtl/pigen_primitives.sv'], directory)
    prefix = 's_t' if family == 'axi4_stream' else 's_w'
    extra = f',.{prefix}keep(payload[7:4])' if family == 'axi4_stream' else f',.{prefix}strb(payload[7:4])' + (f',.{prefix}last(1\'b1)' if family == 'axi4' else '')
    tb.write_text(f'''module tb;
reg clk=0,reset=1;always #5 clk=~clk;
reg [31:0] payload=0;reg offered=0,ready=0;wire accepted,valid;
wire [{bits-1}:0] result;
native_fields dut(.clk(clk),.reset(reset),.{prefix}data(payload),.{prefix}valid(offered),.{prefix}ready(accepted){extra},.result(result),.result_valid(valid),.result_ready(ready));
integer cycle=0,sent=0,received=0,checks=0;
reg [31:0] rng=32'h641729;
reg stalled=0;reg [{bits-1}:0] held;
function [31:0] word(input integer n);word=32'h87231b05+n*32'h10402081;endfunction
function [{bits-1}:0] expected(input [31:0] v);expected={reference};endfunction
always @(posedge clk) begin
 cycle=cycle+1;
 if(reset)begin sent=0;received=0;stalled=0;end
 else begin
  if(stalled&&(!valid||result!==held))$fatal(1,"projection changed under stall");
  stalled=valid&&!ready;held=result;
  if(offered&&accepted)sent=sent+1;
  if(valid&&ready)begin
   if(received>=sent||result!==expected(word(received)))$fatal(1,"projection %0d got %h expected %h",received,result,expected(word(received)));
   received=received+1;checks=checks+1;
  end
  if(cycle>403&&received==512)begin $display("PASS native dynamic projections %0d checked results",checks);$finish;end
 end
 if(cycle>6000)$fatal(1,"projection timeout");
end
always @(negedge clk)begin
 rng=rng*1664525+1013904223;
 reset=cycle<3||cycle==401||cycle==402;
 if(cycle==401&&sent<=received)$fatal(1,"queued reset witness empty");
 offered=!reset&&sent<512;payload=word(sent);
 ready=!reset&&rng[4:2]!=0&&!(cycle>=390&&cycle<=402);
end
endmodule
''')
    run(['iverilog', '-g2012', '-s', 'tb', '-o', sim, sv, tb, ROOT / 'rtl/pigen_primitives.sv'], directory)
    assert 'PASS native dynamic' in run(['vvp', sim], directory)
    print(f'PASS {family} {datatype} child={child}: runtime bit and +:/-: field selects, stalls and queued reset')


if __name__ == '__main__':
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        for datatype in ('uint[32]', 'int[32]', 'bit[4][8]'):
            for child in (False, True):
                case(family, datatype, child)
