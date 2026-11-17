#!/usr/bin/env python3
"""Native boundaries remain available; whole-interface fabrics are inactive."""
from pathlib import Path
import subprocess
import tempfile
from native_protocol_test import case

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pigen-native-quarantine-') as tmp:
    root = Path(tmp)
    count = 0
    for family, options in [('axi4_stream', ''), ('axi4_lite', '[addr=32]'), ('axi4', '[addr=32,id=2]')]:
        ports = f'input wire clk,input wire reset,input uint[32] {family}{options} s,output uint[32] {family}{options} m'
        relay = 'm<=s;' if family == 'axi4_stream' else 'm.aw<=s.aw;m.w<=s.w;s.b<=m.b;m.ar<=s.ar;s.r<=m.r;'
        child = f'module relay({ports});always @(posedge clk) begin {relay} end endmodule\n'
        positive = child + f'module top({ports});relay child(.clk(clk),.reset(reset),.s(s),.m(m));endmodule'
        source, output = root/'case.pigen', root/'case.sv'
        source.write_text(positive)
        subprocess.run([ROOT/'pigen', source, '-o', output], check=True, capture_output=True)
        subprocess.run(['verilator', '--lint-only', '--top-module', 'top', output, ROOT/'rtl/pigen_primitives.sv'], check=True, capture_output=True)
        for body in ['fabric begin s -> m; endfabric', 'fabric begin s > m; endfabric', 'if(1) begin fabric begin s -> m; endfabric end']:
            source.write_text(f'module top({ports});{body} endmodule')
            output.write_text('preserved')
            result = subprocess.run([ROOT/'pigen', source, '-o', output], text=True, capture_output=True)
            assert result.returncode and 'native AXI fabrics are not in use' in result.stderr, result.stderr
            assert output.read_text() == 'preserved'
            count += 1
        source.write_text(f'fabric top({ports}) begin s -> m; endfabric')
        result = subprocess.run([ROOT/'pigen', source, '-o', output], text=True, capture_output=True)
        assert result.returncode and 'native AXI fabrics are not in use' in result.stderr, result.stderr
        count += 1
    print(f'PASS {count} inactive native fabric cases; all-family boundary transfers and child bindings pass fatal lint')

# Retain independent stalled standard-pin transport coverage at module boundaries.
with tempfile.TemporaryDirectory(prefix='pigen-native-boundaries-') as tmp:
    root = Path(tmp)
    for index, config in enumerate([
        ('axi4_stream', 32, 'buf', False),
        ('axi4_stream', 32, 'skid', True),
        ('axi4_lite', 32, 'buf', False),
        ('axi4_lite', 64, 'fifo[3]', False),
        ('axi4', 8, 'buf', False),
        ('axi4', 256, 'skid', False),
        ('axi4', 1024, 'buf', False),
    ]):
        directory = root/str(index)
        directory.mkdir()
        case(directory, *config)
