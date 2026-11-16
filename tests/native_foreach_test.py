#!/usr/bin/env python3
"""Native collection foreach checked through independent standard pins."""
from pathlib import Path
import tempfile
import subprocess
import re
from native_protocol_test import ROOT, channels, run


def case(directory, family, mode):
    fields=channels(family,32)
    options={'axi4_stream':'keep=1,strb=1,last=1,id=4,dest=3,user=7','axi4_lite':'addr=40',
             'axi4':'addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13'}[family]
    protocol=f'bit[32] {family}[{options}] buf'
    coords=[(a,b) for a in range(-2,1) for b in (3,2)]
    member='beat' if family=='axi4_stream' else 'w'
    channel='t' if family=='axi4_stream' else 'w'
    child=mode in ('child','generated_child','unbraced_child')
    target='leaf.m' if mode in ('child','unbraced_child') else 'bank[0].leaf.m' if child else 'm' if mode=='fabric' else 's'
    low,high,top,bottom=(8,10,1,0) if child else (-2,0,3,2)
    source=''
    if child:
        source=f'module relay(input wire clk,input wire reset,input {protocol} s[8:10][1:0],output {protocol} m[8:10][1:0]);\n'
        source+='for(genvar a=8;a<=10;a++) begin : rows for(genvar b=1;b>=0;b--) begin : columns always @(posedge clk) if(!reset) begin\n'
        for ch in fields:
            name='beat' if ch=='t' else ch
            dst,src=('s','m') if ch in ('b','r') else ('m','s')
            source+=f'{dst}[a][b].{name} <= {src}[a][b].{name};\n'
        source+='end end end endmodule\n'
        if mode=='unbraced_child':
            source=source.replace('begin : rows ', '').replace('begin : columns ', '').replace('end end end endmodule', 'end endmodule')
    source+=f'module native_foreach(input wire clk,input wire reset,input {protocol} s[-2:0][3:2]'
    if mode=='fabric': source+=f',output {protocol} m[-2:0][3:2]'
    source+=',output logic [31:0] checksum,output logic [31:0] exits,output logic [31:0] skipped,output logic [31:0] nested,output logic [5:0] seen);\n'
    if child:
        if mode=='generated_child': source+='for(genvar g=0;g<1;g++) begin : bank\n'
        source+='relay leaf(.clk(clk),.reset(reset),.s(s));\n'
        if mode=='generated_child': source+='end\n'
    if mode=='fabric': source+='fabric begin s -> m; endfabric\n'
    source+='always_comb begin int sum; sum=0; seen=0;\n'
    source+=f'''foreach({target}[i,j]) begin
 localparam W=32; typedef logic [W-1:0] word_t; word_t piece; int carry;
 if(i=={low} && j=={top}) carry=0;
 piece=peek({target}[i][j].{member}).data;
 carry+=int'(piece);
 sum=sum*3+carry+i*17+j;
 for(int k=i;k<i+2;k++) sum+=k;
 seen[uint[3]'((i-({low}))*2+({top}-j))]=valid({target}[i][j].{member});
end
checksum=32'(sum); sum=0;
foreach({target}[i,j]) begin
 if(i=={low} && j=={bottom}) continue;
 if(i=={low+1} && j=={top}) break;
 sum=sum*3+int'(peek({target}[i][j].{member}).data);
end
exits=32'(sum); sum=0;
foreach({target}[,j]) begin
 for(int j=0;j<2;j++) sum+=j;
 sum=sum*7+j+int'(peek({target}[{low}][j].{member}).data);
end
skipped=32'(sum); sum=0;
foreach({target}[i]) foreach({target}[,j]) begin
 int carry;
 if(i=={low} && j=={top}) carry=0;
 carry+=i*5+j;sum+=carry;
end
nested=32'(sum);
end endmodule
'''
    if mode in ('automatic','array_initializer'):
        source=source.replace('word_t piece; int carry;', f'automatic word_t piece=peek({target}[i][j].{member}).data; automatic int carry=i-i+j-j;')
        source=source.replace(f' piece=peek({target}[i][j].{member}).data;\n','')
        source=source.replace(' int carry;\n', ' automatic int carry=i-i+j-j;\n')
        if mode=='array_initializer':
            source=source.replace(f'automatic word_t piece=peek({target}[i][j].{member}).data;',
                f"automatic word_t samples[1:0]='{{peek({target}[i][j].{member}).data,32'(i-i+j-j)}}; automatic word_t piece=samples[1];")
    if mode=='named':
        first=source.index(f'foreach({target}[i,j]) begin\n localparam')
        last=source.index('\nchecksum=',first)
        segment=source[first:last]
        head,body=segment.split('\n if(',1)
        head=head.replace('begin\n','begin : sample\n',1)
        body=re.sub(r'\b(piece|carry)\b',r'sample.\1',body)
        assert body.endswith('\nend')
        source=source[:first]+head+'\n if('+body+' : sample'+source[last:]
    pigen,sv,tb,sim=[directory/name for name in ('foreach.pigen','foreach.sv','tb.sv','sim')]
    pigen.write_text(source)
    run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','native_foreach',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    declarations=['reg clk=0,reset=1; always #5 clk=~clk; wire [31:0] checksum,exits,skipped,nested; wire [5:0] seen;']
    connections=['.clk(clk)','.reset(reset)','.checksum(checksum)','.exits(exits)','.skipped(skipped)','.nested(nested)','.seen(seen)']
    drive,clear,change,accept=[],[],[],[]
    for lane,(a,b) in enumerate(coords):
        suffix=(f'm{-a}' if a<0 else str(a))+f'_{b}'
        for port in ('s','m') if mode=='fabric' else ('s',):
            for ch,layout in fields.items():
                forward=ch not in ('b','r')
                for field,width in layout+[('valid',1),('ready',1)]:
                    incoming=(forward != (field=='ready')) if port=='s' else (forward == (field=='ready'))
                    pin=f'{port}_{suffix}_{ch}{field}'
                    declarations.append(f'{"reg" if incoming else "wire"} [{width-1}:0] {pin}'+('=0;' if incoming else ';'))
                    connections.append(f'.{pin}({pin})')
        valid=f's_{suffix}_{channel}valid';ready=f's_{suffix}_{channel}ready';data=f's_{suffix}_{channel}data'
        drive += [f'{valid}=1;',f'{data}=32\'(trial*37+{lane}*11+3);']
        clear.append(f'{valid}=0;')
        change.append(f'{data}=~{data};')
        accept.append(f'if({valid} && {ready}) accepted[{lane}]=1;')
    tb.write_text('module tb;\n'+'\n'.join(declarations)+'\nnative_foreach dut('+','.join(connections)+');\n'+f'''
integer trial,cycles,a,b,sum,carry,lane;
reg [5:0] accepted; reg [31:0] word,expected;
initial begin
 for(trial=0;trial<128;trial=trial+1) begin
  @(negedge clk); reset=1; {' '.join(clear)}
  repeat(2) @(negedge clk);
  reset=0; accepted=0; {' '.join(drive)}
  for(cycles=0;cycles<16 && accepted!=6'b111111;cycles=cycles+1) begin
   @(posedge clk); {' '.join(accept)}
   @(negedge clk);
   {''.join(f'if(accepted[{n}]) '+clear[n] for n in range(6))}
  end
  if(accepted!=6'b111111) $fatal(1,"native foreach input capacity/handshake failed");
  repeat(8) @(negedge clk);
  sum=0;carry=0;lane=0;
  for(a={low};a<={high};a=a+1) for(b={top};b>={bottom};b=b-1) begin
   word=32'(trial*37+lane*11+3);carry={'0+' if mode in ('automatic','array_initializer') else 'carry+'}int'(word);sum=sum*3+carry+a*17+b+(2*a+1);lane=lane+1;
  end
  expected=32'(sum);
  if(checksum!==expected || seen!==6'b111111) $fatal(1,"native foreach lane/order/static identity mismatch got=%h expected=%h seen=%b",checksum,expected,seen);
  if(exits!==32'(trial*37+3)) $fatal(1,"native foreach multidimensional exits failed");
  sum=0;
  for(b={top};b>={bottom};b=b-1) begin sum=sum+1;sum=sum*7+b+(trial*37+({top}-b)*11+3);end
  if(skipped!==32'(sum)) $fatal(1,"native foreach skipped dimension or nested shadow failed");
  sum=0;carry=0;
  for(a={low};a<={high};a=a+1) for(b={top};b>={bottom};b=b-1) begin carry={'0+' if mode in ('automatic','array_initializer') else 'carry+'}a*5+b;sum=sum+carry;end
  if(nested!==32'(sum)) $fatal(1,"nested native foreach static-local identity failed");
  {' '.join(change)} repeat(3) @(negedge clk);
  if(checksum!==expected || seen!==6'b111111) $fatal(1,"native foreach observation drained or bypassed storage");
 end
 $display("PASS native foreach {family} {mode}: 128 resets, six lanes, rebase/order/static locals, exits and peek retention");$finish;
end
initial begin #1000000;$fatal(1,"native foreach timeout");end
endmodule
''')
    if mode=='array_initializer':
        run(['verilator','--binary','--timing','--top-module','tb','--Mdir',directory/'obj','-o','sim',sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
        output=run([directory/'obj/sim'],directory)
    else:
        run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
        output=run(['vvp',sim],directory)
    assert 'PASS native foreach' in output
    print(output.strip())


def negatives(root):
    header='module bad(input wire clk,input wire reset,input bit[32] axi4_stream buf s[2],output logic [31:0] q); always_comb begin q=0; '
    failures={
        'write':'foreach(s[i]) i=0;',
        'increment':'foreach(s[i]) i++;',
        'array_name':'foreach(s[s]) q=0;',
        'rank':'foreach(s[i,j]) q=0;',
        'scope':'foreach(s[i]) q=32\'(i); q=32\'(i);',
        'parameter':'foreach(s[i]) begin localparam N=i; q=32\'(N); end',
        'type':'foreach(s[i]) begin typedef logic[i+1:0] word_t; word_t x; x=0;q=32\'(x); end',
        'shape':'foreach(s[i]) begin logic x[i+1]; q=0; end',
        'consumption':'foreach(s[i]) q=s[i].data;',
    }
    for name,body in failures.items():
        source=root/(name+'.pigen');sv=root/(name+'.sv')
        source.write_text(header+body+' end endmodule');sv.write_text('preserved\n')
        result=subprocess.run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        assert result.returncode and sv.read_text()=='preserved\n',(name,result.stdout)
    print('PASS native foreach negatives: immutable indices, constant declarations, rank/scope and buffered ownership')


def main():
    with tempfile.TemporaryDirectory(prefix='pigen-native-foreach-') as temporary:
        root=Path(temporary)
        for family in ('axi4_stream','axi4_lite','axi4'):
            for mode in ('direct','named','automatic','array_initializer','child','generated_child','unbraced_child','fabric'):
                directory=root/(family+'_'+mode);directory.mkdir();case(directory,family,mode)
        negatives(root)


if __name__=='__main__':
    main()
