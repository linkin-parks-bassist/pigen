#!/usr/bin/env python3
"""Run the real AXI cores through native fabrics against independent pin benches."""
from pathlib import Path
import argparse
import subprocess

from native_protocol_test import ROOT


def run(args, directory):
    result = subprocess.run(list(map(str, args)), cwd=directory, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    with (directory / "commands.log").open("a") as log:
        log.write("$ " + " ".join(map(str, args)) + "\n" + result.stdout)
    assert result.returncode == 0, result.stdout
    return result.stdout


def case(directory, family, hops, direct, seed, boundary=False, standalone=False, internal=False, stored_input=False):
    full = family == 'axi4'
    stream = family == 'axi4_stream'
    name = 'pigen_axis_fifo' if stream else 'pigen_axi4_bridge' if full else 'pigen_axil_registers'
    original = (ROOT / ('cores/axis/fifo.pigen' if stream else 'cores/axi4/bridge.pigen' if full else 'cores/axil/registers.pigen')).read_text()
    start = original.index('module ')
    header = original[start:original.index(');', start) + 2]
    core = original.replace('module ' + name, 'module target_core', 1)
    protocol = 'uint[32] ' + family + ('[keep=1,last=1]' if stream else '[addr=32' + (',id=4' if full else '') + ']')
    relay = f'module link(input wire clk,input wire reset,input {protocol} s,output {protocol} m);\n'
    relay += 'always @(posedge clk) if (!reset) begin\n'
    for channel in (('beat',) if stream else ('aw', 'w', 'b', 'ar', 'r')):
        dst, src = ('s', 'm') if channel in ('b', 'r') else ('m', 's')
        relay += f'{dst}.{channel} <= {src}.{channel};\n'
    relay += 'end\nendmodule\n'
    if internal:
        arrow = '>' if direct else '->'
        relay = f'module link(input wire clk,input wire reset,input {protocol} s,output {protocol} m);\n'
        relay += f'{protocol} fifo[3] middle;\nfabric begin s {arrow} middle; middle {arrow} m; endfabric\nendmodule\n'
    if standalone:
        typed = protocol.replace('uint[32]', 'uint[WIDTH]')
        body = (f'typedef {typed} local_bus_t; typedef local_bus_t copied_bus_t;\n'
                f'copied_bus_t fifo[3] middle[1];\ns {">" if direct else "->"} middle[0]; middle[0] {">" if direct else "->"} m;\n') if internal else 's -> m;\n'
        relay = f'fabric link #(parameter WIDTH=64)(input wire clk,input wire reset,input {typed} s,output {typed} m) begin\n{body}endfabric\n'
    wrapper = header + '\n'
    if stored_input:
        wrapper += f'{protocol} fifo[3] stored;\n'
    backend = ',.m_axis(m_axis)' if stream else ''
    if full:
        backend = ''.join(f',.{port}({port})' for port in ('write_request', 'write_reply', 'read_request', 'read_reply'))
    binding = f',.{"s_axis" if stream else "s_axi"}(stored)' if stored_input else ''
    wrapper += f'target_core target(.clk(clk),.reset(reset){backend}{binding});\n'
    for i in range(hops):
        ingress = (',.s(s_axis)' if stream else ',.s(s_axi)') if i == 0 and not boundary else ''
        wrapper += f'link {"#(.WIDTH(32)) " if standalone else ""}hop{i}(.clk(clk),.reset(reset){ingress});\n'
        destination = f'hop{i+1}.s' if i + 1 < hops else 'target.s_axis' if stream else 'target.s_axi'
        if stored_input and i + 1 == hops: destination = 'stored'
        wrapper += f'fabric route{i} begin hop{i}.m {">" if direct else "->"} {destination}; endfabric\n'
    if boundary:
        destination = 'hop0.s' if hops else 'target.s_axis' if stream else 'target.s_axi'
        if stored_input and not hops: destination = 'stored'
        wrapper += f'fabric ingress begin {"s_axis" if stream else "s_axi"} {">" if direct else "->"} {destination}; endfabric\n'
    wrapper += 'endmodule\n'
    source = directory / 'core.pigen'; sv = directory / 'core.sv'
    source.write_text(core + relay + wrapper)
    run([ROOT / 'pigen', source, '-o', sv, '--no-diagram'], directory)
    run(['verilator', '--lint-only', '--top-module', name, sv, ROOT / 'rtl/pigen_primitives.sv'], directory)
    top = 'axis_fabric_core_tb' if stream else 'axi4_tb' if full else 'axil_tb'
    bench = directory / 'tb.sv' if stream else ROOT / 'tests/cores' / (top + '.sv')
    if stream: bench.write_text(STREAM_BENCH)
    sim = directory / 'sim'
    run(['iverilog', '-g2012', '-s', top, '-P', f'{top}.SEED={seed}', '-o', sim,
         sv, ROOT / 'rtl/pigen_primitives.sv', bench], directory)
    result = run(['vvp', sim], directory)
    assert 'PASS ' in result, result
    print(f'PASS {family} real core: {hops} {"direct" if direct else "routed"} hops boundary={boundary} standalone={standalone} internal={internal} stored_input={stored_input} seed={seed}: independent transactions, stalls, errors and reset')


STREAM_BENCH = r"""
module axis_fabric_core_tb;
 parameter integer SEED=1;
 reg clk=0,reset=1;
 always #5 clk=~clk;
 reg [31:0] s_axis_tdata=0;
 reg [3:0] s_axis_tkeep=0;
 reg s_axis_tlast=0,s_axis_tvalid=0,m_axis_tready=0;
 wire s_axis_tready,m_axis_tvalid,m_axis_tlast;
 wire [31:0] m_axis_tdata;
 wire [3:0] m_axis_tkeep;
 pigen_axis_fifo dut(.*);
 integer cycle=0,sent=0,received=0,epochs=0,checks=0;
 reg [31:0] rng=SEED;
 reg stalled=0,accepted=0;
 reg [36:0] held;
 function [31:0] payload(input integer n);payload=32'h13579bdf+n*32'h10204081;endfunction
 always @(posedge clk) begin
  cycle=cycle+1;
  if(reset) begin sent=0;received=0;stalled=0;accepted=0;end
  else begin
   if(stalled && (!m_axis_tvalid || {m_axis_tlast,m_axis_tkeep,m_axis_tdata}!==held)) $fatal(1,"stalled stream changed");
   stalled=m_axis_tvalid&&!m_axis_tready;held={m_axis_tlast,m_axis_tkeep,m_axis_tdata};
   accepted=s_axis_tvalid&&s_axis_tready;
   if(accepted)sent=sent+1;
   if(m_axis_tvalid&&m_axis_tready) begin
    if(received>=sent || m_axis_tdata!==payload(received) || m_axis_tkeep!==4'(received%16) || m_axis_tlast!==(received%7==6)) $fatal(1,"stream order/data/sidebands");
    received=received+1;checks=checks+1;
   end
   if(epochs==2 && received==300) begin $display("PASS stream core fabric %0d checks, queued reset and stalls",checks);$finish;end
  end
  if(cycle>10000)$fatal(1,"stream fabric timeout");
 end
 always @(negedge clk) begin
  rng=rng*1664525+1013904223;
  reset=(cycle<3 || cycle==81 || cycle==82 || cycle==431 || cycle==432);
  if(cycle==81 || cycle==431)begin
   if(sent<=received)$fatal(1,"reset must discard queued stream traffic");
   epochs=epochs+1;
  end
  if(reset)begin s_axis_tvalid=0;m_axis_tready=0;end
  else begin
   s_axis_tvalid=(s_axis_tvalid&&!accepted) || (sent<300 && rng[4:2]!=0);
   s_axis_tdata=payload(sent);s_axis_tkeep=4'(sent%16);s_axis_tlast=(sent%7==6);
   m_axis_tready=rng[7:5]!=0 && !(cycle>=50&&cycle<=82) && !(cycle>=400&&cycle<=432);
  end
 end
endmodule
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT / 'build/native-core-fabrics')
    root = parser.parse_args().out.resolve()
    root.mkdir(parents=True, exist_ok=True)
    for family in ('axi4_stream', 'axi4_lite', 'axi4'):
        for direct in (True, False):
            for seed in (1, 17, 4919):
                directory = root / f'{family}-stored-input-{direct}-{seed}'
                directory.mkdir(exist_ok=True)
                (directory / 'commands.log').write_text('')
                case(directory, family, 0, direct, seed, boundary=True, stored_input=True)
        for direct in (True, False):
            for seed in (1, 17, 4919):
                for standalone in (False, True):
                    directory = root / f'{family}-internal-{direct}-{standalone}-{seed}'
                    directory.mkdir(exist_ok=True)
                    (directory / 'commands.log').write_text('')
                    case(directory, family, 1, direct, seed, boundary=True, internal=True, standalone=standalone)
        for hops, direct, boundary, standalone in ((1, True, False, False), (1, False, False, False), (2, False, False, False), (0, True, True, False), (0, False, True, False), (1, False, True, False), (1, False, False, True)):
            for seed in (1, 17, 4919):
                directory = root / f'{family}-{hops}-{direct}-{boundary}-{standalone}-{seed}'
                directory.mkdir(exist_ok=True)
                (directory / 'commands.log').write_text('')
                case(directory, family, hops, direct, seed, boundary, standalone)


if __name__ == '__main__':
    main()
