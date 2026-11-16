#!/usr/bin/env python3
"""Contextual unpacked values compared with independently elaborated native SV."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run


def main():
    body='''
 typedef struct packed {logic[3:0] hi;logic[3:0] lo;} record_t;
 localparam FIRST=-2;
 automatic uint[8] marker=uint[8]'(data);
 automatic logic[7:0] values[-2:-1]='{default:'1,FIRST:data};
 automatic logic[7:0] mapped[4:3]=values;
 automatic logic[15:0] widened[0:1]='{values[-2],values[-1]};
 static logic[7:0] seeds[1:0]='{8'h12,8'h34};
 static logic[7:0] startup_copy[2:1]=seeds;
 automatic logic[15:0] default_sized[1:0]='{default:8'hff+8'h1};
 automatic logic[15:0] entry_sized[1:0]='{8'hff+8'h1,8'hff+8'h1};
 automatic logic[7:0] rows[2:1][-1:0]='{2{'{data,8'h5a}}};
 automatic logic[7:0] defaults[1:0][3:2]='{default:data};
 automatic record_t records[1:0]='{'{hi:data[7:4],lo:data[3:0]},'{default:4'hc}};
 automatic record_t record_defaults[1:0]='{default:8'h5a};
 automatic logic[7:0] flat[0:1]=records;
 automatic logic[7:0] chosen[1:0]=select ? '{data,8'h11} : '{8'h22,~data};
 logic[31:0] sum;
 result=0;sum=0;
 result[15:0]={values[-2],values[-1]};result[31:16]={mapped[4],mapped[3]};
 result[63:32]={widened[0],widened[1]};result[79:64]={startup_copy[2],startup_copy[1]};
 result[111:80]={rows[2][-1],rows[2][0],rows[1][-1],rows[1][0]};
 result[143:112]={defaults[1][3],defaults[1][2],defaults[0][3],defaults[0][2]};
 result[159:144]={records[1].hi,records[1].lo,records[0].hi,records[0].lo};
 result[175:160]={chosen[1],chosen[0]};result[191:176]={flat[0],flat[1]};
 for(int i=0;i<3;i++) begin
  automatic logic[7:0] entry[2:1]='{8'(i),data};
  sum+=32'(entry[2])+32'(entry[1]);entry[1]=0;
 end
 result[223:192]=sum;
 mapped='{8'h33,data};result[239:224]={mapped[4],mapped[3]};
 values[-2]=0;rows[2][-1]=0;defaults[1][3]=0;chosen[1]=0;
 result[247:240]=marker;
 result[255:248]={2'b0,record_defaults[1]==8'h5a,1'b0,default_sized[1]==16'h100,2'b0,entry_sized[1]==16'h100};
'''
    source='module unpacked_init(input logic[7:0] data,input logic select,output logic[255:0] result);always_comb begin'+body+'end endmodule\n'
    # Keep independent SV patterns/copies. Spell the named constant key as its
    # literal value for Verilator 5.020 and make reference widening explicit.
    native=body.replace("uint[8]'(data)","byte_t'(data)").replace('uint[8]','byte_t')
    native=native.replace('FIRST:data',"32'shfffffffe:data").replace("'{default:data}","'{default:'{default:data}}").replace("'{values[-2],values[-1]}","'{16'(values[-2]),16'(values[-1])}")
    native=native.replace("8'hff+8'h1","16'h00ff+16'h0001")
    native='\ntypedef bit[7:0] byte_t;\n'+native
    oracle='module oracle(input logic[7:0] data,input logic select,output logic[255:0] result);always_comb begin'+native+'end endmodule\n'
    bench='''module tb;logic[7:0] data=0;logic select=0;wire[255:0] result,expected;integer trial;
unpacked_init dut(.*);oracle reference(.data(data),.select(select),.result(expected));
initial begin for(trial=0;trial<1024;trial++) begin
 data=8'(trial*31+7);select=trial[0];#1;
 if(result!==expected) $fatal(1,"unpacked initializer trial=%0d got=%h expected=%h",trial,result,expected);
end $display("PASS unpacked initialization: 1024 native SV comparisons, bounds mapping, patterns, records, conditionals and entry lifetimes");$finish;end endmodule
'''
    with tempfile.TemporaryDirectory(prefix='pigen-unpacked-init-') as temporary:
        root=Path(temporary)
        pigen,sv,reference,tb=[root/name for name in ('init.pigen','init.sv','oracle.sv','tb.sv')]
        pigen.write_text(source);reference.write_text(oracle);tb.write_text(bench)
        run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root)
        run(['verilator','--lint-only','--top-module','unpacked_init',sv],root)
        run(['verilator','--binary','--timing','--top-module','tb','--Mdir',root/'obj','-o','sim',sv,reference,tb],root)
        print(run([root/'obj/sim'],root).strip())
        for declaration in (
            "automatic logic[7:0] a[1:0]='{1};",
            "automatic logic[7:0] a[1:0]='{1,2,3};",
            "automatic logic[7:0] a[1:0][1:0]='{1,2};",
            "automatic logic[7:0] a[1:0]='{0:1};",
            "automatic logic[7:0] a[1:0]='{0:1,0:2,default:0};",
            "automatic logic[7:0] a[1:0]='{default:0,default:1};",
            "automatic logic[7:0] a[1:0]='{2:1,default:0};",
            "automatic logic[7:0] a[1:0]='{data:1,default:0};",
            "automatic logic[7:0] a[1:0]='{0{1}};",
            "automatic logic[7:0] a[1:0]='{-1{1}};",
            "automatic logic[7:0] a[1:0]='{data{1}};",
            "automatic logic[7:0] a[1:0]='{token,0};",
            "automatic logic[7:0] a[1:0];automatic logic[15:0] b[1:0]=a;",
            "automatic logic[7:0] a[1:0];automatic bit[7:0] b[1:0]=a;",
            "automatic logic signed[7:0] a[1:0];automatic logic[7:0] b[1:0]=a;",
            "automatic logic[7:0] a[1:0];automatic logic[7:0] b[2:0]=a;",
            "automatic logic[7:0] a[1:0];logic[7:0] b;b=a;",
            "automatic logic[7:0] a[1:0];static logic[7:0] b[1:0]='{a[1],0};",
        ):
            pigen.write_text('module bad(input logic[7:0] data,input logic[7:0] buf token,output logic[7:0] result);always_comb begin uint[8] marker;'+declaration+'marker=0;result=0;end endmodule')
            sv.write_text('preserved output\n')
            run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],root,expect=False)
            assert sv.read_text()=='preserved output\n'
        print('PASS unpacked errors preserve output: cardinality, rank, keys, replication, element equivalence, buffered reads and static capture')


if __name__=='__main__':
    main()
