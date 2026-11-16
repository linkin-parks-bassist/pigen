#!/usr/bin/env python3
"""Independent beat scoreboard for the Stream routing primitive."""
from pathlib import Path
import shutil
import tempfile
from native_protocol_test import ROOT, run


def case(directory, count, dest_width):
    directory.mkdir()
    width, offset, records = 79, 11, 1200
    wrapper = f'''module dut(input logic clk,reset,in_valid,output logic in_ready,
input logic [{width-1}:0] packet_in,output logic [{count-1}:0] out_valid,
input logic [{count-1}:0] out_ready,output logic [{count*width-1}:0] packet_out);
pigen_axis_demux #(.PAYLOAD_T(logic [{width-1}:0]),.N({count}),.DEST_LSB({offset}),.DEST_W({dest_width})) core(.*);
endmodule
'''
    invalid = "33'h100000001" if dest_width == 33 else f"{dest_width}'d{count}"
    tb = f'''module tb;
localparam N={count}, W={width}, DW={dest_width}, LSB={offset}, RECORDS={records};
logic clk=0,reset=1,in_valid=0,in_ready;
logic [W-1:0] packet_in='0;
logic [N-1:0] out_valid,out_ready='0;
logic [N*W-1:0] packet_out;
dut device(.*);
always #5 clk=~clk;
logic [W-1:0] expected[N][0:4095],held[N];
integer writes[N],reads[N],sent=0,received=0,cycles=0,epoch=0,blocked=0;
logic [N-1:0] stalled='0;
function automatic [W-1:0] beat(input integer seqno);
reg [W-1:0] value;
begin
value={{32'(seqno*19+7),32'(seqno),15'(seqno*37)}};
value[LSB+:DW]=DW'((seqno/5)%N);
value[0]=(seqno%5)==4;
beat=value;
end endfunction
always @(negedge clk) begin
if(reset) begin in_valid=0;out_ready='0;end
else begin
for(integer lane=0;lane<N;lane++) out_ready[lane]=epoch==0 || (cycles>15 && ((cycles*7+lane*3)%13)>3);
if(!in_valid || in_ready) begin
in_valid=epoch==0 || (sent<RECORDS && (cycles%9)<6);
packet_in=beat(sent);
if(epoch==0) packet_in[LSB+:DW]={invalid};
end
end end
always @(posedge clk) begin : score
integer selected,valid_count;
if(reset) begin
sent=0;received=0;cycles=0;blocked=0;stalled='0;
for(integer lane=0;lane<N;lane++) begin writes[lane]=0;reads[lane]=0;held[lane]='0;end
end else begin
cycles=cycles+1;
if(in_valid && in_ready) begin
if(epoch==0) blocked=blocked+1;
else begin
selected=int'(packet_in[LSB+:DW]);
if(selected<0 || selected>=N) $fatal(1,"test source route");
expected[selected][writes[selected]]=packet_in;writes[selected]=writes[selected]+1;
sent=sent+1;
end end
valid_count=0;
for(integer lane=0;lane<N;lane++) begin
if(out_valid[lane]) valid_count=valid_count+1;
if(epoch==0 && out_valid[lane]) $fatal(1,"unmapped TDEST escaped");
if(stalled[lane] && (!out_valid[lane] || packet_out[lane*W+:W] !== held[lane])) $fatal(1,"unstable held output");
stalled[lane]=out_valid[lane] && !out_ready[lane];held[lane]=packet_out[lane*W+:W];
if(out_valid[lane] && out_ready[lane]) begin
if(reads[lane]>=writes[lane] || packet_out[lane*W+:W] !== expected[lane][reads[lane]]) $fatal(1,"route, record or order mismatch");
reads[lane]=reads[lane]+1;received=received+1;
end end
if(valid_count>1) $fatal(1,"beat replicated across routes");
if(epoch==1 && received==RECORDS) begin
if(sent!=RECORDS) $fatal(1,"incorrect accepted count");
$display("PASS Stream demux N=%0d DEST_W=%0d records=%0d",N,DW,received);$finish;
end
end end
initial begin
repeat(3) @(negedge clk);#1 reset=0;
repeat(20) @(negedge clk);#1;
if(blocked!=2 || in_ready || out_valid!='0) $fatal(1,"unmapped route must fill two registered slots then block");
reset=1;
repeat(3) @(negedge clk);#1 epoch=1;reset=0;
end
initial begin #200000;$fatal(1,"demux timeout");end
endmodule
'''
    sv, test, binary = directory/'dut.sv', directory/'tb.sv', directory/'sim'
    sv.write_text(wrapper)
    test.write_text(tb)
    primitives = ROOT/'rtl/pigen_primitives.sv'
    run(['verilator','--lint-only','--top-module','dut',sv,primitives],directory)
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,test,primitives],directory)
    assert 'PASS Stream demux' in run(['vvp',binary],directory)
    print(f'PASS Stream routing primitive: outputs={count}, TDEST={dest_width}, records={records}, stalls/reset/unmapped routes')


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-axis-demux-'))
    try:
        for count,dest_width in ((1,1),(2,3),(3,3),(5,3),(3,33)):
            case(root/f'n{count}_w{dest_width}',count,dest_width)
    except Exception:
        print(f'demux evidence retained at {root}')
        raise
    else:
        shutil.rmtree(root)


if __name__ == '__main__':
    main()
