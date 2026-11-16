#!/usr/bin/env python3
"""AXI belongs on module/fabric ports; child views bridge ordinary transfers."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run, case


def negatives(root):
    count = 0
    for family, options in (('axi4_stream','last=1'), ('axi4_lite','addr=32'), ('axi4','addr=32,id=4')):
        contract = f'uint[32] {family}[{options}]'
        prefix = f'typedef {contract} bus_t; package buses; typedef {contract} bus_t; endpackage\n'
        declarations = (f'{contract} local;', 'bus_t local;', 'buses::bus_t local;',
                        f'{contract} fifo[3] local[2];', f'if(1)begin:g {contract} local;end',
                        f'for(genvar i=0;i<2;i++)begin:g {contract} local;end',
                        f'always @(posedge clk) begin {contract} local;end',
                        'always @(posedge clk) begin bus_t local;end',
                        'always_comb begin buses::bus_t local;end')
        for index, declaration in enumerate(declarations):
            source = root/f'bad_{family}_{index}.pigen'; output=source.with_suffix('.sv')
            source.write_text(prefix+f'module bad(input wire clk,input wire reset);{declaration} endmodule')
            output.write_text('preserve output\n')
            result=run([ROOT/'pigen',source,'-o',output,'--no-diagram'],root,expect=False)
            assert ('AXI declarations are only allowed on module or fabric input/output ports' in result or
                    'procedural locals must be static variables, not ports or AXI endpoints' in result),result
            assert output.read_text()=='preserve output\n'
            count += 1
        source=root/f'bad_fabric_{family}.pigen';output=source.with_suffix('.sv')
        source.write_text(prefix+f'fabric bad(input wire clk,input wire reset,input {contract} s,output {contract} m) begin bus_t local;s -> local;local -> m;endfabric')
        result=run([ROOT/'pigen',source,'-o',output,'--no-diagram'],root,expect=False)
        assert ('AXI declarations are only allowed on module or fabric input/output ports' in result or
                    'procedural locals must be static variables, not ports or AXI endpoints' in result),result
        count += 1
    print(f'PASS {count} boundary-only AXI declaration diagnostics',flush=True)


def child_records(root):
    source=root/'records.pigen';sv=source.with_suffix('.sv');tb=root/'tb.sv';sim=root/'sim'
    source.write_text((ROOT/'cores/axil/registers.pigen').read_text()+'''
module records(input wire clk,input wire reset,input uint[32] buf address,output bit[34] buf response);
    pigen_axil_registers regs(.clk(clk),.reset(reset));
    always @(posedge clk) if(!reset) begin
        regs.s_axi.ar <= '{addr:address,prot:0};
        response <= {regs.s_axi.r.data,regs.s_axi.r.resp};
    end
endmodule
''')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],root)
    run(['verilator','--lint-only','--top-module','records',sv,ROOT/'rtl/pigen_primitives.sv'],root)
    tb.write_text('''module tb;
logic clk=0,reset=1;logic[31:0]address=0;logic address_valid=0;wire address_ready;
wire[33:0]response;wire response_valid;logic response_ready=0;int sent=0,got=0,cycle=0;logic took;
records dut(.*);always #5 clk=~clk;
initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)if(!reset)begin
 response_ready=cycle>30&&(cycle%7)>2;
 if(!address_valid||took)begin address_valid=sent<97;address=32'((sent%5)*4);end
end
always @(posedge clk)if(!reset)begin
 took=address_valid&&address_ready;if(took)sent++;
 if(response_valid&&response_ready)begin
  if(response!==34'((got%5)==4?3:0))$fatal(1,"child record DATA/RESP mismatch");got++;
 end
 cycle++;if(got==97)begin if(sent!=97)$fatal(1,"request count");$display("PASS child AXI records: 97 ordinary requests/replies, stalls and errors");$finish;end
 if(cycle>3000)$fatal(1,"timeout");
end
endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],root)
    print(run(['vvp',sim],root).strip(),flush=True)


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-native-boundary-'))
    print(root,flush=True)
    negatives(root)
    child_records(root)
    for family in ('axi4_stream','axi4_lite','axi4'):
        for hierarchy in (True,'siblings','fabric_routed'):
            directory=root/f'{family}-{hierarchy}';directory.mkdir()
            case(directory,family,32,hierarchy=hierarchy)

if __name__=='__main__':main()
