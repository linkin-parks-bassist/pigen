#!/usr/bin/env python3
"""Independent standard-pin scoreboard for native Stream fabric routing."""
from pathlib import Path
import shutil
import tempfile
from native_protocol_test import ROOT, run


def case(directory, sources, destinations, mode='boundary', dest_width=3, id_bits=4, metadata=False):
    directory.mkdir()
    composition=mode
    if mode.startswith('legacy_both_interleaved_'):
        composition=mode[len('legacy_both_interleaved_'):]
        mode='legacy_both_interleaved'
    fields=[(field,bits) for field,bits in [('data',32),('keep',4),('strb',4),('id',id_bits),('dest',dest_width),('user',7),('last',1)] if bits]
    carrier = 'user' if metadata else 'data'
    if metadata:
        fields = [(field,bits) for field,bits in [('id',id_bits),('dest',dest_width),('user',32),('last',1)] if bits]
    hierarchy=mode.startswith('hierarchy')
    legacy=mode.startswith('legacy_')
    legacy_interleaved=legacy and mode!='legacy_exact'
    groups=(sources+2)//3
    source_id_bits=0 if mode in ('absent_ids','hierarchy_absent') else id_bits
    target_id_bits=source_id_bits+((2+(groups-1).bit_length()) if hierarchy else 0 if legacy else (sources-1).bit_length())
    interleaved=legacy_interleaved or mode in ('interleaved','id_interleaved','colliding_interleaved','absent_ids') or hierarchy
    id_option=f'id={source_id_bits},' if source_id_bits else ''
    protocol=f'uint[32] axi4_stream[keep=1,strb=1,{id_option}dest={dest_width},user=7,last=1,clock=ACLK,resetn=ARESETn]'
    if metadata:
        protocol=f'void axi4_stream[{id_option}dest={dest_width},user=32,last=1,clock=ACLK,resetn=ARESETn]'
    target_protocol=protocol.replace(f'id={source_id_bits},',f'id={target_id_bits},') if source_id_bits else protocol.replace('dest=',f'id={target_id_bits},dest=')
    array_modes=('collections','generated_arrays','mixed_explicit','mixed_implicit','mixed_nested','mixed_regions','mixed_tags')
    controls='input wire ACLK,input wire ARESETn'
    ports=[f'input {protocol} s{i}' for i in range(sources)]+[f'output {target_protocol} m{i}' for i in range(destinations)]
    if composition in array_modes: ports=[f'input {protocol} s[{sources}]',f'output {target_protocol} m[{destinations}]']
    source=''
    if composition=='child':
        source=''.join(f'module {name}({controls},input {kind} s,output {kind} m); always @(posedge ACLK) if(ARESETn) m<=s; endmodule\n' for name,kind in [('source_relay',protocol),('target_relay',target_protocol)])
    standalone=composition=='standalone'
    source+=f'{"fabric" if standalone else "module"} native_route({controls},'+','.join(ports)+(') begin\n' if standalone else ');\n')
    if composition=='child':
        source+=''.join(f'source_relay input{i}(.ACLK(ACLK),.ARESETn(ARESETn),.s(s{i}));\n' for i in range(sources))
        source+=''.join(f'target_relay output{i}(.ACLK(ACLK),.ARESETn(ARESETn),.m(m{i}));\n' for i in range(destinations))
    if composition=='generated': source+='for(genvar block=0;block<1;block++) begin : selected\n'
    for i in range(sources):
        for j in (reversed(range(destinations)) if mode=='order' and i%2 else range(destinations)):
            if mode=='hybrid' and i>0 and j>0: continue
            left=f'input{i}.m' if composition=='child' else f's{i}'
            right=f'output{j}.s' if composition=='child' else f'm{j}'
            if composition in array_modes: left,right=f's[{i}]',f'm[{j}]'
            link=f'{left} -> {right};'
            source+=link+'\n' if standalone else f'fabric route_{i}_{j} begin {link} endfabric\n'
    if composition=='generated': source+='end\n'
    source+='endfabric\n' if standalone else 'endmodule\n'
    if composition=='generated_arrays':
        source=f'module native_route({controls},'+','.join(ports)+f'); for(genvar a=0;a<{sources};a++) begin : inputs for(genvar b=0;b<{destinations};b++) begin : outputs fabric begin s[a] -> m[b]; endfabric end end endmodule\n'
    if mode.startswith('mixed_'):
        assert destinations==6
        def links(index):
            return 'fabric begin '+''.join(f's[{i}] -> m[{index}];' for i in (reversed(range(sources)) if mode=='mixed_tags' else range(sources)))+'endfabric\n'
        middle='for(genvar lane=0;lane<2;lane++) begin : middle '+links('2*lane+1')+links('2*lane+2')+'end\n'
        if mode=='mixed_explicit': middle='generate '+middle+'endgenerate\n'
        if mode=='mixed_nested': middle='if(1) begin : selected '+middle+'end\n'
        if mode=='mixed_regions': middle='generate if(1) begin : early '+links('1')+'end endgenerate\n'+links('2')+'generate for(genvar lane=3;lane<5;lane++) begin : later '+links('lane')+'end endgenerate\n'
        source=f'module native_route({controls},'+','.join(ports)+');\n'
        source+=links('0')+'if(0) begin : disabled '+links('99')+'end\n'+middle+links('5')+'endmodule\n'
    if hierarchy:
        middle_bits=source_id_bits+2
        middle_protocol=protocol.replace(f'id={source_id_bits},',f'id={middle_bits},') if source_id_bits else protocol.replace('dest=',f'id={middle_bits},dest=')
        source=''
        for group in range(groups):
            group_size=min(3,sources-group*3)
            gports=[f'input {protocol} p{i}' for i in range(group_size)]+[f'output {middle_protocol} m{i}' for i in range(destinations)]
            source+=f'fabric group{group}({controls},'+','.join(gports)+') begin\n'
            for j in range(destinations):
                for i in (reversed(range(group_size)) if group%2 else range(group_size)):
                    source+=f'p{i} -> m{j};\n'
            source+='endfabric\n'
        source+=f'module native_route({controls},'+','.join(ports)+');\n'
        for group in range(groups):
            bindings=','.join(f'.p{i}(s{group*3+i})' for i in range(min(3,sources-group*3)))
            source+=f'group{group} first{group}(.ACLK(ACLK),.ARESETn(ARESETn),'+bindings+');\n'
        for j in range(destinations):
            for group in reversed(range(groups)):
                source+=f'fabric begin first{group}.m{j} -> m{j}; endfabric\n'
        source+='endmodule\n'
    pigen,sv,tb,binary=[directory/n for n in ('route.pigen','route.sv','tb.sv','sim')]
    pigen.write_text(source)
    run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory)
    if metadata: assert '_tdata' not in sv.read_text()
    run(['verilator','--lint-only','--top-module','native_route',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    declarations=[];connections=['.ACLK(clk)','.ARESETn(!reset)'];drive=[];check=[]
    for kind,count in [('s',sources),('m',destinations)]:
        for lane in range(count):
            for field,bits in fields+[('valid',1),('ready',1)]:
                if field=='id':
                    bits=source_id_bits if kind=='s' else target_id_bits
                    if not bits: continue
                pin=f'{kind}{lane}_t{field}'
                declarations.append(f'logic [{bits-1}:0] {pin};')
                port=f'{kind}_{lane}_t{field}' if composition in array_modes else pin
                connections.append(f'.{port}({pin})')
    for lane in range(sources):
        drive += [f'if(reset) s{lane}_tvalid=0; else if(!s{lane}_tvalid || accepted[{lane}]) begin',
                  f's{lane}_tvalid=sent[{lane}]<RECORDS && ((cycles+{lane})%7)<5;']
        for index,(field,bits) in enumerate(fields):
            if field=='id' and not source_id_bits: continue
            value=(f'({lane}*65536+sent[{lane}]) | (epoch==0 ? 32\'h80000000 : 0)' if field==carrier else
                   f'(sent[{lane}]/5)%D' if field=='dest' else f'{lane}' if field=='id' else
                   f'(sent[{lane}]%5)==4' if field=='last' else f'sent[{lane}]*{index+3}+{lane}*7')
            if mode=='unmapped' and field=='dest': value=f'(epoch==0 ? D : {value})'
            if mode=='hybrid' and lane>0 and field=='dest': value='0'
            if interleaved and field=='dest': value=f'(sent[{lane}]+{lane})%D'
            if interleaved and field=='last': value=f'((sent[{lane}]/D)%5)==4'
            if (mode.startswith('colliding') or hierarchy or legacy) and field=='id': value='7'
            if mode=='id_interleaved' and field=='id': value=f'(sent[{lane}]/D)%2'
            if mode=='id_interleaved' and field=='last': value=f'((sent[{lane}]/(D*2))%5)==4'
            if legacy_interleaved:
                keys=4 if mode=='legacy_both_interleaved' else 2
                if field=='id': value=f'sent[{lane}]%2' if mode!='legacy_dest_interleaved' else '7'
                if field=='dest': value=f'(sent[{lane}]/2)%2' if mode=='legacy_both_interleaved' else f'sent[{lane}]%2' if mode=='legacy_dest_interleaved' else '0'
                if field=='last': value=f'((sent[{lane}]/{keys})%3)==2'
                if mode=='legacy_reopen_interleaved':
                    if field=='id': value=f'(sent[{lane}]%6==1 || sent[{lane}]%6==4)'
                    if field=='last': value=f'(sent[{lane}]%6==2 || sent[{lane}]%6==4 || sent[{lane}]%6==5)'
                if mode=='legacy_full_namespace':
                    if field=='id': value=f'sent[{lane}]%16'
                    if field=='dest': value=f'(sent[{lane}]/16)%8'
                    if field=='last': value=f'sent[{lane}]>=128'
                if field=='id': value=f'(epoch==0 ? 15 : ({value}))'
                if field=='dest': value=f'(epoch==0 ? 7 : ({value}))'


            drive.append(f's{lane}_t{field}={bits}\'({value});')
        drive.append('end')
        packet='{'+','.join(f's{lane}_t{field}' for field,_ in fields if field!='id' or source_id_bits)+'}'
        check += [f'if(source_stalled[{lane}] && (!s{lane}_tvalid || {packet}!==held_source[{lane}])) $fatal(1,"source changed under stall");',
                  f'source_stalled[{lane}]=s{lane}_tvalid && !s{lane}_tready;held_source[{lane}]={packet};',
                  f'accepted[{lane}]=s{lane}_tvalid && s{lane}_tready;',
                  f'if(accepted[{lane}]) sent[{lane}]=sent[{lane}]+1;']
    for lane in range(destinations):
        drive.append(f'm{lane}_tready=(epoch==1 || {int(mode=='unmapped')} || ({int(legacy_interleaved)} && received<4)) && !reset && ((cycles*7+{lane}*3)%13)>3;')
        packet='{'+','.join(f'm{lane}_t{field}' for field,_ in fields if field!='id' or target_id_bits)+'}'
        check += [f'if(stalled[{lane}] && (!m{lane}_tvalid || {packet} !== held[{lane}])) $fatal(1,"unstable routed record");',
                  f'stalled[{lane}]=m{lane}_tvalid && !m{lane}_tready;held[{lane}]={packet};',
                  f'if(m{lane}_tvalid && m{lane}_tready) begin',
                  f'if(m{lane}_t{carrier}[31] !== (epoch==0)) $fatal(1,"reset epoch leaked");',
                  f'who=int\'((m{lane}_t{carrier} & 32\'h7fffffff)>>16); seqno=int\'(m{lane}_t{carrier}&65535);',
                  f'if(who<0 || who>=S || seqno!=next_seq[who][{lane}] || seqno>=sent[who]) $fatal(1,"route/order/duplicate mismatch");',
                  f'if({int(destinations==1)} && owner[{lane}]!=-1 && owner[{lane}]!=who) $fatal(1,"packet arbitration changed before TLAST");',
                  f'owner[{lane}]=who;']
        for index,(field,bits) in enumerate(fields):
            if field=='id':
                bits=target_id_bits
                if not bits: continue
            expected=(f'{lane}' if field=='dest' else 'who' if field=='id' else '(seqno%5)==4' if field=='last' else f'seqno*{index+3}+who*7')
            if interleaved and field=='last': expected='((seqno/D)%5)==4'
            if field=='id':
                input_id='7' if (mode.startswith('colliding') or hierarchy or legacy) else '(seqno/D)%2' if mode=='id_interleaved' else 'who' if source_id_bits else '0'
                expected=f'(who<<{source_id_bits})|({input_id})'
            if legacy and field=='id': expected=input_id
            if hierarchy and field=='id': expected=f'(origin_tag(who)<<{source_id_bits})|({input_id if source_id_bits else 0})'
            if mode=='mixed_tags' and field=='id': expected=f'((S-1-who)<<{source_id_bits})|({input_id})'
            if mode=='id_interleaved' and field=='last': expected='((seqno/(D*2))%5)==4'
            if mode=='order' and field=='dest': expected=f'(who%2 ? D-1-{lane} : {lane})'
            if legacy_interleaved:
                if field=='id': expected='seqno%2' if mode!='legacy_dest_interleaved' else '7'
                if field=='dest': expected='(seqno/2)%2' if mode=='legacy_both_interleaved' else 'seqno%2' if mode=='legacy_dest_interleaved' else '0'
                if field=='last': expected=f'((seqno/{4 if mode=="legacy_both_interleaved" else 2})%3)==2'
                if mode=='legacy_reopen_interleaved':
                    if field=='id': expected='(seqno%6==1 || seqno%6==4)'
                    if field=='last': expected='(seqno%6==2 || seqno%6==4 || seqno%6==5)'
                if mode=='legacy_full_namespace':
                    if field=='id': expected='seqno%16'
                    if field=='dest': expected='(seqno/16)%8'
                    if field=='last': expected='seqno>=128'
                if field=='id': expected=f'(epoch==0 ? 15 : ({expected}))'
                if field=='dest': expected=f'(epoch==0 ? 7 : ({expected}))'


            if field!=carrier: check.append(f'if(m{lane}_t{field} !== {bits}\'({expected})) $fatal(1,"routed {field} mismatch");')
        if legacy_interleaved:
            check += [f'if(packet_owner[int\'(m{lane}_tid)][int\'(m{lane}_tdest)]!=-1 && packet_owner[int\'(m{lane}_tid)][int\'(m{lane}_tdest)]!=who) $fatal(1,"interleaved packet changed origin before matching TLAST");',
                      f'packet_owner[int\'(m{lane}_tid)][int\'(m{lane}_tdest)]=m{lane}_tlast ? -1 : who;']
        check += [f'next_seq[who][{lane}]=seqno+({int(interleaved)} ? D : {int(mode=='hybrid')} && who>0 ? 1 : ((seqno%5)==4 ? 5*(D-1)+1 : 1));',
                  f'if(m{lane}_tlast) owner[{lane}]=-1;', 'received=received+1;end']
    width=sum(bits for field,bits in fields if field!='id')+target_id_bits
    tag_function=''
    if hierarchy:
        entries=[]
        for who in range(sources):
            group=who//3;group_size=min(3,sources-group*3)
            local=group_size-1-who%3 if group%2 else who%3
            tag=((groups-1-group)<<2)|local
            entries.append(f'{who}:origin_tag={tag};')
        tag_function='function automatic integer origin_tag(input integer who);case(who)'+''.join(entries)+'default:origin_tag=-1;endcase endfunction'
    tb.write_text(f'''module tb;
localparam S={sources},D={destinations},RECORDS={256 if mode=="legacy_full_namespace" else 60*destinations};
logic clk=0,reset=1;integer epoch=0,cycles=0,received=0,who,seqno;
integer packet_owner[16][8],open_packets;
integer sent[S],next_seq[S][D],owner[D];logic [D-1:0] stalled='0;
logic [{width-1}:0] held[D];
logic [S-1:0] accepted='0,source_stalled='0;logic [{width-target_id_bits+source_id_bits-1}:0] held_source[S];
{tag_function}
{chr(10).join(declarations)}
native_route dut({','.join(connections)});
always #5 clk=~clk;
always @(negedge clk) begin {chr(10).join(drive)} end
always @(posedge clk) if(reset) begin
received=0;cycles=0;stalled='0;accepted='0;source_stalled='0;
for(integer i=0;i<S;i++) begin sent[i]=0;for(integer j=0;j<D;j++) next_seq[i][j]={int(interleaved)} ? (j-i%D+D)%D : ({int(mode=='order')} && i%2 ? D-1-j : j)*5;end
for(integer j=0;j<D;j++) owner[j]=-1;
for(integer i=0;i<16;i++) for(integer j=0;j<8;j++) packet_owner[i][j]=-1;
end else begin cycles=cycles+1;{chr(10).join(check)}
if(epoch==1 && received==S*RECORDS) begin $display("PASS native Stream routing");$finish;end
end
initial begin
repeat(3) @(negedge clk);#1 reset=0;
repeat(35) @(negedge clk);#1;
if(sent[0]==0 || ({int(not legacy_interleaved)} && received!=0)) $fatal(1,"queued-reset setup failed");
if({int(legacy_interleaved)}) begin
open_packets=0;for(integer i=0;i<16;i++) for(integer j=0;j<8;j++) if(packet_owner[i][j]>=0) open_packets++;
if(received!=4 || open_packets==0) $fatal(1,"reset requires accepted partial interleaved packets");end
reset=1;
repeat(3) @(negedge clk);#1 epoch=1;reset=0;
end
initial begin #2000000;$fatal(1,"route timeout");end
endmodule
''')
    run(['iverilog','-g2012','-s','tb','-o',binary,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    assert 'PASS native Stream routing' in run(['vvp',binary],directory)
    print(f'PASS native Stream routing {sources}x{destinations} {mode}/{composition} TDEST={dest_width}: standard pins, fields, packets, stalls and queued reset')


def negatives(directory):
    protocol='uint[32] axi4_stream[dest=2,last=1]'
    parent=f'module bad(input wire clk,input wire reset,input wire other,input {protocol} s,output {protocol} m0,output {protocol} m1,output {protocol} m2);'
    cases={
        'duplicate': parent+'fabric begin s -> m0; s -> m0; endfabric endmodule',
        'direct_first': parent+'fabric begin s > m0; s -> m1; endfabric endmodule',
        'direct_last': parent+'fabric begin s -> m0; s > m1; endfabric endmodule',
        'missing_dest': (parent+'fabric begin s -> m0; s -> m1; endfabric endmodule').replace('dest=2,',''),
        'insufficient_dest': (parent+'fabric begin s -> m0; s -> m1; s -> m2; endfabric endmodule').replace('dest=2','dest=1'),
        'clock': parent.replace(f'output {protocol} m1',f'output {protocol.replace("last=1]","last=1,clock=other]")} m1')+'fabric begin s -> m0; s -> m1; endfabric endmodule',
        'contract': parent.replace(f'output {protocol} m1',f'output {protocol.replace("uint[32]","uint[64]")} m1')+'fabric begin s -> m0; s -> m1; endfabric endmodule',
        'local_owner': parent+'fabric begin s -> m0; s -> m1; endfabric always @(posedge clk) if(!reset) m2<=s; endmodule',
    }
    wide=protocol.replace('dest=2','dest=6')
    cases['route_limit']=f'module bad(input wire clk,input wire reset,input {wide} s,'+','.join(f'output {wide} m{i}' for i in range(33))+'); fabric begin '+''.join(f's -> m{i};' for i in range(33))+'endfabric endmodule'
    equal_ids=parent.replace('dest=2','id=4,dest=2').replace(f'input {protocol} s,',f'input {protocol} s,')
    equal_ids=equal_ids.replace(' s,',' s0,').replace('output uint[32]', 'input uint[32] axi4_stream[id=4,dest=2,last=1] s1,output uint[32]',1)
    cases['switched_ids']=equal_ids+'fabric begin s0 -> m0; s0 -> m1; s1 -> m0; endfabric endmodule'
    cases['late_switched_ids']=equal_ids+'fabric begin s0 -> m0; s1 -> m0; s0 -> m1; endfabric endmodule'
    bounded='uint[32] axi4_stream[id=8,dest=3,last=1]'
    bounded_source=f'module bad(input wire clk,input wire reset,input {bounded} s0,input {bounded} s1,output {bounded} m); fabric begin s0 -> m; s1 -> m; endfabric endmodule'
    cases['packet_tracking_limit']=bounded_source
    for family in ('axi4_lite','axi4'):
        cases[family]=(parent+'fabric begin s -> m0; s -> m1; endfabric endmodule').replace('axi4_stream[dest=2,last=1]',family)
    for name,source in cases.items():
        pigen,sv=directory/f'{name}.pigen',directory/f'{name}.sv'
        pigen.write_text(source);sv.write_text('preserved output\n')
        output=run([ROOT/'pigen',pigen,'-o',sv,'--no-diagram'],directory,expect=False)
        expected={'clock':'clock domains','contract':'compatible native','local_owner':'another transfer owner','insufficient_dest':'sufficient TDEST','route_limit':'at most 32','switched_ids':'provenance bits','late_switched_ids':'provenance bits','packet_tracking_limit':'packet tracking exceeds generated-bit policy'}
        assert expected.get(name,'another fabric consumer') in output,(name,output)
        assert sv.read_text()=='preserved output\n',name
    # Widening the target reserves provenance rather than allocating a
    # bitmap for the entire source identity namespace.
    widened=directory/'tracking_widened.pigen';widened_sv=directory/'tracking_widened.sv'
    widened.write_text(bounded_source.replace(f'output {bounded}',f'output {bounded.replace("id=8","id=9")}'))
    run([ROOT/'pigen',widened,'-o',widened_sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','bad',widened_sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    no_last=directory/'tracking_no_last.pigen';no_last_sv=directory/'tracking_no_last.sv'
    no_last.write_text(bounded_source.replace('last=1','last=0'))
    run([ROOT/'pigen',no_last,'-o',no_last_sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','bad',no_last_sv,ROOT/'rtl/pigen_primitives.sv'],directory)

    print('PASS native Stream route negatives: TDEST, direct links, contracts, timing, ownership and output preservation')


def metadata_routing_tests(root):
    for mode in ('boundary', 'child', 'standalone', 'generated_arrays', 'colliding_interleaved', 'absent_ids', 'hierarchy'):
        case(root / ('metadata_' + mode), 7 if mode == 'hierarchy' else 3, 3, mode, metadata=True)


def main():
    root=Path(tempfile.mkdtemp(prefix='pigen-axis-routing-'))
    try:
        metadata_routing_tests(root)
        for sources,destinations,mode in ((2,1,'boundary'),(2,1,'absent_ids'),(1,2,'boundary'),(1,3,'boundary'),(2,3,'boundary'),(3,5,'boundary'),(2,3,'child'),(2,3,'standalone'),(2,3,'generated'),(3,5,'collections'),(2,3,'generated_arrays'),(2,3,'order'),(2,3,'hybrid'),(2,3,'interleaved'),(3,3,'interleaved'),(2,3,'id_interleaved'),(2,3,'colliding'),(2,3,'colliding_interleaved'),(2,3,'absent_ids'),(1,3,'unmapped')):
            case(root/f'{sources}_{destinations}_{mode}',sources,destinations,mode)
        for mode in ('mixed_explicit','mixed_implicit','mixed_nested','mixed_regions','mixed_tags'):
            case(root/mode,2,6,mode)
        for sources,mode in ((5,'hierarchy'),(7,'hierarchy_absent')):
            case(root/mode,sources,3,mode)
        case(root/'legacy_exact',3,1,'legacy_exact')
        case(root/'legacy_no_identity',3,1,'legacy_exact',0,0)
        for mode in ('legacy_id_interleaved','legacy_dest_interleaved','legacy_both_interleaved','legacy_reopen_interleaved','legacy_full_namespace'):
            case(root/mode,3,1,mode)
        for composition in ('child','standalone','generated','collections','generated_arrays'):
            case(root/('legacy_'+composition),3,1,'legacy_both_interleaved_'+composition)
        case(root/'wide',1,3,'boundary',33)
        negatives(root)
    except Exception:
        print(f'routing evidence retained at {root}')
        raise
    else:
        shutil.rmtree(root)


if __name__=='__main__':
    main()
