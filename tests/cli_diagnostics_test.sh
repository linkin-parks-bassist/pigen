#!/bin/sh
# Exercise the real CLI with isolated malformed, EOF and successful inputs.
set -eu
make pigen
python3 - <<'PYTHON'
from pathlib import Path
import subprocess
import tempfile
compiler=str(Path('pigen').resolve())
with tempfile.TemporaryDirectory() as name:
    directory=Path(name)
    (directory/'invalid.pigen').write_text('module bad;\n    buf bit[8] stage;\nendmodule\n')
    def invoke(source,*args):
        return subprocess.run([compiler,source,*args],cwd=directory,text=True,capture_output=True)
    result=invoke('invalid.pigen')
    assert result.returncode==1 and result.stdout=='',result
    assert result.stderr==('invalid.pigen:2:5: data type must precede the transfer type\n'
                          '        buf bit[8] stage;\n        ^\n'),result.stderr
    assert not (directory/'invalid.sv').exists()
    # A zero-width EOF diagnostic takes the driver's bare-prefix fallback.
    (directory/'unfinished.pigen').write_text('module unfinished;\n')
    result=invoke('unfinished.pigen')
    assert result.returncode==1 and result.stdout==''
    assert result.stderr=='pigen: unterminated SystemVerilog design unit\n',result.stderr
    assert not (directory/'unfinished.sv').exists()
    # Diagnostics cannot overwrite an existing output artifact.
    (directory/'kept.sv').write_text('sentinel\n')
    result=invoke('invalid.pigen','-o','kept.sv')
    assert result.returncode==1 and (directory/'kept.sv').read_text()=='sentinel\n'
    (directory/'good.pigen').write_text('module good(input logic clk,d,output logic q); always @(posedge clk) q<=d; endmodule\n')
    result=invoke('good.pigen','-o','good.sv')
    assert result.returncode==0 and result.stdout=='' and result.stderr=='',result
    assert (directory/'good.sv').stat().st_size>0
print('PASS CLI spanned/EOF diagnostics, silent success and failed-output preservation')
PYTHON
