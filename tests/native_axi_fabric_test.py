#!/usr/bin/env python3
"""Legal independent masters/target at flattened pins; no compiler layouts."""
from pathlib import Path
import shutil
import tempfile

from native_protocol_test import ROOT, run, channels


def case(directory, family, data, count, bare=False, exclusive=False, extra_id_bits=0, direct=False, boundary=False, standalone=False, collections=False, mapped=False, switched=False, tree=False, ):
    full = family == "axi4"
    fields = channels(family, data)
    if bare:
        fields = {ch: [(field, bits) for field, bits in channel if field not in ("id", "user")]
                  for ch, channel in fields.items()}
    config = "addr=40" + (",id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13" if full and not bare else "")
    protocol = f"uint[{data}] {family}[{config},clock=ACLK,resetn=ARESETn]"
    source_id_bits = 0 if bare else 4
    groups=(count+2)//3
    target_id_bits = source_id_bits + ((2+(groups-1).bit_length()) if tree else (count-1).bit_length()) + extra_id_bits
    target_fields = fields
    target_protocol = protocol
    if full:
        target_fields = {ch: [(field, target_id_bits if field == "id" else bits)
                              for field, bits in channel if not (bare and field == "user")]
                         for ch, channel in channels(family, data).items()}
        if bare:
            target_config = config + f",id={target_id_bits}"
        else:
            target_config = config.replace("id=4", f"id={target_id_bits}")
        target_protocol = f"uint[{data}] {family}[{target_config},clock=ACLK,resetn=ARESETn]"
    controls = "input wire ACLK,input wire ARESETn"
    source = f"module relay({controls},input {protocol} s,output {protocol} m);\n"
    source += "always @(posedge ACLK) if(ARESETn) begin\n"
    for ch in fields:
        dst, src = ("s", "m") if ch in ("b", "r") else ("m", "s")
        source += f"{dst}.{ch} <= {src}.{ch};\n"
    source += "end\nendmodule\n"
    source += source.replace("module relay(", "module target_relay(").replace(protocol, target_protocol)
    ports = [f"input {protocol} s{i}" for i in range(count)] + [f"output {target_protocol} m"]
    if collections:
        assert boundary
        ports = [port + '[1]' for port in ports]
    source += f"{'fabric' if standalone else 'module'} native_merge({controls}," + ",".join(ports) + (") begin\n" if standalone else ");\n")
    if boundary:
        for i in range(count):
            source += f"s{i} {'>' if direct else '->'} m;\n" if standalone else f"fabric link{i} begin s{i} {'>' if direct else '->'} m; endfabric\n"
    else:
        source += "target_relay sink(.ACLK(ACLK),.ARESETn(ARESETn),.m(m));\n"
        for i in range(count):
            source += f"relay source{i}(.ACLK(ACLK),.ARESETn(ARESETn),.s(s{i}));\n"
            source += f"fabric link{i} begin source{i}.m {'>' if direct else '->'} sink.s; endfabric\n"
    source += "endfabric\n" if standalone else "endmodule\n"
    if mapped:
        source=source.replace(" -> m;", " -> m at [0:40'hffffffffff];").replace(" -> sink.s;", " -> sink.s at [0:40'hffffffffff];")
    if switched:
        assert boundary and not collections and not standalone and not exclusive
        top=source.index('module native_merge(')
        source=source[:top]+source[top:].replace(f'output {target_protocol} m)',f'output {target_protocol} m[2])')
        for i in range(count):
            source=source.replace(f's{i} -> m;',f"s{i} -> m[0] at [0:40'hffffffff]; s{i} -> m[1] at [40'h100000000:40'h1ffffffff];")
    if tree:
        assert boundary and not standalone and not collections and not switched and not direct
        middle_config=(config+",id=2") if bare else config.replace('id=4',f'id={source_id_bits+2}')
        middle_protocol=f'uint[{data}] {family}[{middle_config},clock=ACLK,resetn=ARESETn]' if full else protocol
        source=''
        for group in range(groups):
            size=min(3,count-group*3)
            inputs=[f'input {protocol} p{i}' for i in range(size)]
            source+=f'fabric group{group}({controls},'+','.join(inputs+[f'output {middle_protocol} m'])+') begin\n'
            for i in (reversed(range(size)) if group%2 else range(size)):
                source+=f'p{i} -> m;\n'
            source+='endfabric\n'
        source+=f'module native_merge({controls},'+','.join(ports)+');\n'
        for group in range(groups):
            bindings=','.join(f'.p{i}(s{group*3+i})' for i in range(min(3,count-group*3)))
            source+=f'group{group} first{group}(.ACLK(ACLK),.ARESETn(ARESETn),'+bindings+');\n'
        # A deliberately different outer order makes the accumulated prefix
        # distinguishable from a flat source number.
        for group in reversed(range(groups)):
            source+=f'fabric begin first{group}.m -> m; endfabric\n'
        source+='endmodule\n'
    pigen, sv, tb, binary = [directory / name for name in ("merge.pigen", "merge.sv", "tb.sv", "sim")]
    pigen.write_text(source)
    run([ROOT / "pigen", pigen, "-o", sv, "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", "native_merge", sv,
         ROOT / "rtl/pigen_primitives.sv"], directory)

    def value(ch, field, who, seq, beat="0", target=False):
        """Independent legal channel values, with colliding IDs across masters."""
        if field == "addr":
            if exclusive == "shared":
                return f"(({seq})*4096)"
            return f"(({who})*64'h1000000 + ({seq})*4096" + (f" + (({seq})%2)*64'h100000000)" if switched else ")")
        if field == "data":
            return f"({{{data}{{1'b1}}}} ^ {data}'(({who})*65536 + ({seq})*512 + ({beat})))"
        if field == "id":
            if full and target:
                origin=f"origin_tag({who})" if tree else who
                return f"(({origin})*{1 << source_id_bits} + {'0' if bare else f'(({seq})%4)'})"
            return f"(({seq})%4)"
        if field == "len":
            return f"(length({seq})-1)"
        if field == "size":
            return f"transfer_size({seq})"
        if field == "burst":
            return f"burst({seq})"
        if field == "lock":
            return "1" if exclusive else "0"
        if field == "last":
            return f"(({beat})==length({seq})-1)"
        if field == "strb":
            if full:
                lane = f"(burst({seq})==0 ? 0 : (({beat})*(1<<transfer_size({seq})))%{data//8})"
                return f"(((1<<(1<<transfer_size({seq})))-1)<<{lane})"
            return f"(({seq})+({beat}))"
        if field == "resp":
            if exclusive:
                if ch == "r":
                    return "1"
                if target:
                    return "exclusive_response"
                return f"(winner[{seq}]==({who}) ? 1 : 0)" if exclusive == "shared" else "1"
            return f"(({seq})%3==0 ? 0 : ({seq})%3==1 ? 2 : 3)"
        if exclusive and field == "cache":
            return "0"
        if exclusive and field == "qos":
            return who
        if exclusive and field == "user" and ch in ("aw", "ar"):
            return who
        # All applicable address attributes and USER channels carry distinct values.
        salt = {"user": 7, "prot": 3, "cache": 5, "qos": 11, "region": 13}[field]
        return f"(({who})*{salt}+({seq})*3+({beat}))"

    declarations, connections = [], [".ACLK(clk)", ".ARESETn(!reset)"]
    init, drive, check = [], [], []
    for endpoint in [f"s{i}" for i in range(count)] + ["m"]:
        for ch, channel in (target_fields if endpoint == "m" else fields).items():
            for field, bits in channel + [("valid", 1), ("ready", 1)]:
                pin = f"{endpoint}_{ch}{field}"
                declarations.append(f"logic [{bits-1}:0] {pin};")
                connections.append(f".{endpoint + '_0' if collections else endpoint}_{ch}{field}({pin})")
    for i in range(count):
        init += [f"s{i}_{ch}valid=0;" for ch in ("aw", "w", "ar")]
        init += [f"s{i}_bready=0;s{i}_rready=0;"]
        for ch, seq, beat, available, delay in [
                ("aw", f"wi[{i}]", "0", f"!aw_sent[{i}]", f"(wi[{i}]%3!=0 || cycle-start_w[{i}]>12)"),
                ("w", f"wi[{i}]", f"wb[{i}]", f"wb[{i}]<length(wi[{i}])", f"(wi[{i}]%3!=1 || cycle-start_w[{i}]>12)"),
                ("ar", f"ri[{i}]", "0", f"!ar_sent[{i}]", "1")]:
            if exclusive:
                available += f" && {'ri' if ch == 'ar' else 'wi'}[{i}]==round"
                if ch != "ar":
                    available += " && released"
                    delay = delay.replace(f"start_w[{i}]", "release_cycle")
            pin = f"s{i}_{ch}"
            drive += [f"if(reset) {pin}valid=0; else if(!held_{pin}) begin",
                      f"{pin}valid={seq}<LIMIT && ({available}) && ({delay}) && (($random(seed)&3)!=0);"]
            for field, bits in fields[ch]:
                drive.append(f"{pin}{field}={bits}'({value(ch, field, str(i), seq, beat)});")
            drive.append("end")
        drive += [f"s{i}_bready=!reset && epoch==1 && (($random(seed)&7)>2);",
                  f"s{i}_rready=!reset && epoch==1 && (($random(seed)&7)>2);"]
        check += [f"if(s{i}_awvalid && s{i}_awready) aw_sent[{i}]=1;",
                  f"if(s{i}_wvalid && s{i}_wready) wb[{i}]=wb[{i}]+1;",
                  f"if(s{i}_arvalid && s{i}_arready) ar_sent[{i}]=1;"]
        for ch, seq, beat in [("b", f"wi[{i}]", "0"), ("r", f"ri[{i}]", f"rb[{i}]")]:
            check.append(f"if(s{i}_{ch}valid && !s{i}_{ch}ready) {ch}_stalls={ch}_stalls+1;")
            check.append(f"if(s{i}_{ch}valid && s{i}_{ch}ready) begin")
            for field, bits in fields[ch]:
                check.append(f'if(s{i}_{ch}{field} !== {bits}\'({value(ch, field, str(i), seq, beat)})) $fatal(1,"source {i} {ch}{field} response route/value");')
            if ch == "b":
                check += [f'if(!aw_sent[{i}] || wb[{i}]!=length(wi[{i}])) $fatal(1,"early B");',
                          f"wi[{i}]=wi[{i}]+1;wb[{i}]=0;aw_sent[{i}]=0;start_w[{i}]=cycle;"]
            else:
                check += [f'if(!ar_sent[{i}]) $fatal(1,"early R");',
                          f"rb[{i}]=rb[{i}]+1; if(rb[{i}]==length(ri[{i}])) begin ri[{i}]=ri[{i}]+1;rb[{i}]=0;ar_sent[{i}]=0;end"]
            check.append("end")

    target_drive = ["m_awready=!reset && !aw_known && (($random(seed)&7)>3);",
                    "m_wready=!reset && wc<256 && (($random(seed)&7)>2);",
                    "m_arready=!reset && !ar_known && (($random(seed)&7)>3);"]
    for ch, who, seq, beat, available in [
            ("b", "aw_who", "aw_seq", "0", "aw_known && wc==length(aw_seq)"),
            ("r", "ar_who", "ar_seq", "target_rb", "ar_known")]:
        target_drive += [f"if(reset) m_{ch}valid=0; else if(!held_m_{ch}) begin",
                         f"m_{ch}valid=({available}) && (($random(seed)&3)!=0);"]
        for field, bits in target_fields[ch]:
            target_drive.append(f"m_{ch}{field}={bits}'({value(ch, field, who, seq, beat, target=True)});")
        target_drive.append("end")
    target_check = []
    for ch, prefix in [("aw", "aw"), ("ar", "ar")]:
        target_check += [f"if(m_{ch}valid && m_{ch}ready) begin",
                         f'if({prefix}_known) $fatal(1,"multiple outstanding target {ch}");',
                         f"{prefix}_known=1; {prefix}_who=integer'(m_{ch}addr>>24); {prefix}_seq=integer'((m_{ch}addr & 40'hffffff)>>12);",
                         f'if({prefix}_who>=N || {prefix}_seq>=LIMIT) $fatal(1,"invalid {ch} identity");']
        if exclusive:
            # A real target keys monitors by observed bus ID, not source identity.
            target_check[-2] = target_check[-2].replace(
                f"integer'(m_{ch}addr>>24)", f"integer'(m_{ch}{'qos' if bare else 'user'})")
            if ch == "aw":
                target_check.append("aw_id=integer'(m_awid);")
            else:
                target_check += ["monitor_valid[integer'(m_arid)]=1;monitor_addr[integer'(m_arid)]=m_araddr;"]
        if ch == "aw":
            target_check += ["aw_cycle=cycle;", "if(length(aw_seq)==256) long_w=long_w+1;"]
        else:
            target_check.append("if(length(ar_seq)==256) long_r=long_r+1;")
        for field, bits in target_fields[ch]:
            target_check.append(f'if(m_{ch}{field} !== {bits}\'({value(ch, field, prefix+"_who", prefix+"_seq", target=True)})) $fatal(1,"target {ch}{field}");')
        target_check.append("end")
    target_check.append("if(m_wvalid && m_wready) begin if(wc==0) first_w_cycle=cycle;")
    for field, bits in fields["w"]:
        declarations.append(f"logic [{bits-1}:0] stored_{field}[0:255];")
        target_check.append(f"stored_{field}[wc]=m_w{field};")
    target_check += ["wc=wc+1;end", "if(aw_known && wc>0 && !order_counted) begin order_counted=1;",
                     "if(first_w_cycle<aw_cycle) wfirst=wfirst+1; if(aw_cycle<first_w_cycle) afirst=afirst+1;end",
                     "if(aw_known && ar_known) overlap=overlap+1;", "if(aw_known) begin",
                     'if(wc>length(aw_seq)) $fatal(1,"write crossed owner/burst boundary");',
                     "for(integer j=0;j<wc;j++) begin"]
    for field, bits in fields["w"]:
        target_check.append(f'if(stored_{field}[j] !== {bits}\'({value("w", field, "aw_who", "aw_seq", "j")})) $fatal(1,"AW/W owner association {field}");')
    target_check += ["end end", "if(m_bvalid && m_bready) begin aw_known=0;wc=0;order_counted=0;end",
                     "if(m_rvalid && m_rready) begin target_rb=target_rb+1; if(target_rb==length(ar_seq)) begin ar_known=0;target_rb=0;end end"]
    if exclusive:
        # Complete the write once, before offering B; response is stable thereafter.
        target_check = [line.replace("aw_known=0;wc=0;order_counted=0;", "aw_known=0;wc=0;order_counted=0;write_decided=0;")
                        for line in target_check]
        complete_write = (
            "if(aw_known && wc==length(aw_seq) && !write_decided) begin write_decided=1;\n"
            "exclusive_response=(monitor_valid[aw_id] && monitor_addr[aw_id]==aw_seq*4096+"
            + ("0" if exclusive == "shared" else "aw_who*64'h1000000") + ") ? 1 : 0;\n"
            "if(exclusive_response==1) begin success_count=success_count+1;winner[aw_seq]=aw_who;"
            f"for(integer q=0;q<{1 << target_id_bits};q++) if(monitor_valid[q] && monitor_addr[q]==monitor_addr[aw_id]) monitor_valid[q]=0;end\n"
            "else fail_count=fail_count+1;end\n")
        # A successful write invalidates reservations for the same address only.
        insert_at = target_check.index("end end") + 1
        target_check.insert(insert_at, complete_write)
    # Monitor every physical producer, including all reverse channel fields.
    stability = []
    for endpoint in [f"s{i}" for i in range(count)] + ["m"]:
        for ch, channel in (target_fields if endpoint == "m" else fields).items():
            pin = f"{endpoint}_{ch}"
            width = sum(bits for _, bits in channel)
            packet = "{" + ",".join(pin+field for field, _ in channel) + "}"
            declarations += [f"logic held_{pin}=0;logic [{width-1}:0] packet_{pin};"]
            stability.append(f'if(held_{pin} && (!{pin}valid || {packet} !== packet_{pin})) $fatal(1,"stalled {pin} changed");')
            stability.append(f"held_{pin}={pin}valid && !{pin}ready;packet_{pin}={packet};")
            init.append(f"held_{pin}=0;")
    reset_state = ("aw_known=0;ar_known=0;wc=0;target_rb=0;cycle=0;quiet=0;order_counted=0;\n"
                   "wfirst=0;afirst=0;b_stalls=0;r_stalls=0;overlap=0;long_w=0;long_r=0;\n"
                   "for(integer i=0;i<N;i++) begin wi[i]=0;ri[i]=0;wb[i]=0;rb[i]=0;aw_sent[i]=0;ar_sent[i]=0;start_w[i]=0;end\n" +
                   "\n".join(x for x in init if x.startswith("held_")))
    exclusive_declarations, exclusive_reset, exclusive_progress, exclusive_checks = "", "", "", ""
    if exclusive:
        exclusive_declarations = (f"logic monitor_valid[0:{(1 << target_id_bits)-1}];logic [39:0] monitor_addr[0:{(1 << target_id_bits)-1}];\n"
                                  "integer aw_id=0,exclusive_response=0,success_count=0,fail_count=0,round=0,release_cycle=0;\n"
                                  "integer winner[0:LIMIT-1];logic write_decided=0,released=0;\n")
        exclusive_reset = (f"for(integer q=0;q<{1 << target_id_bits};q++) begin monitor_valid[q]=0;monitor_addr[q]=0;end\n"
                           "for(integer q=0;q<LIMIT;q++) winner[q]=-1;write_decided=0;exclusive_response=0;success_count=0;fail_count=0;round=0;released=0;release_cycle=0;\n")
        reads_done = " && ".join(f"ri[{i}]>round" for i in range(count))
        writes_done = " && ".join(f"wi[{i}]>round" for i in range(count))
        exclusive_progress = (f"if(!released && {reads_done}) begin released=1;release_cycle=cycle;end\n"
                              f"if({writes_done}) begin round=round+1;released=0;end\n")
        successes = 10 if exclusive == "shared" else count*10
        failures = (count-1)*10 if exclusive == "shared" else 0
        exclusive_checks = f'if(success_count!={successes} || fail_count!={failures}) $fatal(1,"exclusive monitor coverage");\n'
    lengths = "case(seq%5) 0:length=1;1:length=2;2:length=4;3:length=16;4:length=256;endcase" if full else "length=1;"
    burst = "burst=seq%5==0 || seq%5==3 ? 0 : seq%5==1 ? 2 : 1;" if full else "burst=1;"
    size = f"transfer_size=seq%3==0 ? {(data//8).bit_length()-1} : seq%3==1 ? 0 : 1;" if full else f"transfer_size={(data//8).bit_length()-1};"
    if exclusive:
        lengths, burst, size = "length=1;", "burst=1;", f"transfer_size={(data//8).bit_length()-1};"
    done = " && ".join(f"wi[{i}]==LIMIT && ri[{i}]==LIMIT" for i in range(count))
    if switched:
        import re
        # Two independent target agents. The oracle derives identities from pins;
        # each source alternates address windows and both targets contend.
        states='aw_known ar_known aw_who aw_seq ar_who ar_seq wc target_rb aw_cycle first_w_cycle order_counted'.split()
        def second(text):
            text=re.sub(r'\bm_', 'm1_', text)
            text=re.sub(r'\bstored_', 'stored1_', text)
            text=re.sub(r'\bheld_m_', 'held_m1_', text)
            text=re.sub(r'\bpacket_m_', 'packet_m1_', text)
            for state in states: text=re.sub(r'\b'+state+r'\b',state+'1',text)
            return text
        declarations += [second(x) for x in declarations if x.startswith('logic') and any(k in x for k in (' m_',' stored_',' held_m_'))]
        original=[x for x in connections if x.startswith('.m_')]
        connections=[x.replace('.m_','.m_0_') if x.startswith('.m_') else x for x in connections]
        connections += [x.replace('.m_','.m_1_').replace('(m_','(m1_') for x in original]
        target_drive += [second(x) for x in target_drive]
        target_check=[x.replace("addr>>24)","addr>>24)&40'hff") for x in target_check]
        target_check += [second(x) for x in target_check]
        for lane,prefix in enumerate(('m','m1')):
            for ch in ('aw','ar'):
                target_check.append(f'if({prefix}_{ch}valid && {prefix}_{ch}ready) begin if({prefix}_{ch}addr[32]!==1\'b{lane}) $fatal(1,"wrong address target"); switched_{ch}[{lane}]=switched_{ch}[{lane}]+1;end')
        declarations += ['integer switched_aw[2],switched_ar[2];']
        reset_state += '\nfor(integer lane=0;lane<2;lane++) begin switched_aw[lane]=0;switched_ar[lane]=0;end'
        exclusive_checks='for(integer lane=0;lane<2;lane++) if(switched_aw[lane]!=N*LIMIT/2 || switched_ar[lane]!=N*LIMIT/2) $fatal(1,"missing switched target traffic");\n'
        stability += [second(x) for x in stability if 'held_m_' in x]
        reset_state += '\n'+second('aw_known=0;ar_known=0;wc=0;target_rb=0;order_counted=0;')
        second_init=[second(x) for x in init if x.startswith('held_m_')]
        init += second_init
        reset_state += '\n'+'\n'.join(second_init)
        declarations += ['logic aw_known1=0,ar_known1=0,order_counted1=0;integer aw_who1=0,aw_seq1=0,ar_who1=0,ar_seq1=0,wc1=0,target_rb1=0,aw_cycle1=0,first_w_cycle1=0;']
    tag_function=''
    if tree:
        entries=[]
        for who in range(count):
            group=who//3;group_size=min(3,count-group*3)
            local=group_size-1-who%3 if group%2 else who%3
            tag=((groups-1-group)<<2)|local
            entries.append(f'{who}:origin_tag={tag};')
        tag_function='function automatic integer origin_tag(input integer who);case(who)'+''.join(entries)+'default:origin_tag=-1;endcase endfunction\n'
    tb.write_text(
        f"module tb;localparam integer N={count},LIMIT=10;logic clk=0,reset=1;always #5 clk=~clk;\n"
        "integer wi[0:N-1],ri[0:N-1],wb[0:N-1],rb[0:N-1],start_w[0:N-1];\n"
        "logic aw_sent[0:N-1],ar_sent[0:N-1];integer seed=32145,epoch=0,cycle=0;\n"
        "logic aw_known=0,ar_known=0;integer aw_who=0,aw_seq=0,ar_who=0,ar_seq=0,wc=0,target_rb=0;\n"
        "integer aw_cycle=0,first_w_cycle=0,wfirst=0,afirst=0,b_stalls=0,r_stalls=0,overlap=0,long_w=0,long_r=0,quiet=0;logic order_counted=0;\n"
        + exclusive_declarations + tag_function +
        f"function automatic integer length(input integer seq);{lengths} endfunction\n" +
        f"function automatic integer burst(input integer seq);{burst} endfunction\n" +
        f"function automatic integer transfer_size(input integer seq);{size} endfunction\n" +
        "\n".join(declarations) + "\nnative_merge dut(" + ",".join(connections) + ");\n"
        + ("initial begin m_bvalid=0;m_rvalid=0;" + ("m1_bvalid=0;m1_rvalid=0;" if switched else "") + "\n") + "\n".join(init) +
        "\nrepeat(4) @(negedge clk);#1;reset=0;repeat(100) @(negedge clk);#1;reset=1;"
        "repeat(3) @(negedge clk);#1;epoch=1;reset=0;end\n"
        "always @(negedge clk) begin\n" + "\n".join(drive+target_drive) + "\nend\n"
        "always @(posedge clk) if(reset) begin\n" + reset_state + exclusive_reset + "\nend else begin cycle=cycle+1;\n" +
        "\n".join(stability+check+target_check) + "\n" + exclusive_progress +
        f'\nif(epoch==1 && {done}) begin quiet=quiet+1; if(quiet>50) begin\n' +
        ('if(wfirst==0 || afirst==0 || b_stalls==0 || r_stalls==0 || overlap==0) $fatal(1,"missing timing coverage");\n' if not exclusive else '') +
        ('if(long_w==0 || long_r==0) $fatal(1,"missing long bursts");\n' if full and not exclusive else '') +
        exclusive_checks + '$display("PASS native AXI memory fabric");$finish;end end\nend\n' +
        'initial begin #500000; $fatal(1,"memory fabric timeout");end endmodule\n')
    run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, tb,
         ROOT / "rtl/pigen_primitives.sv"], directory)
    assert "PASS native AXI memory fabric" in run(["vvp", binary], directory)
    print(f"PASS {family} data={data} sources={count} bare={bare} exclusive={exclusive} collections={collections} mapped={mapped} switched={switched} tree={tree}: tagged IDs, complete fields, stalls, transactions and queued reset")


def negatives(directory):
    relay = ("module relay(input wire clk,input wire reset,input uint[32] axi4[id=4] s,"
             "output uint[32] axi4[id=4] m);\n"
             "always @(posedge clk) if(!reset) begin m.aw<=s.aw;m.w<=s.w;s.b<=m.b;m.ar<=s.ar;s.r<=m.r;end\nendmodule\n")
    for count, bits in [(2, 4), (3, 5), (5, 6)]:
        target = relay.replace("module relay(", "module target(").replace("id=4", f"id={bits}")
        source = relay + target + "module top(input wire clk,input wire reset);\n"
        source += "target sink(.clk(clk),.reset(reset));\n"
        for i in range(count):
            source += f"relay source{i}(.clk(clk),.reset(reset));\n"
            source += f"fabric link{i} begin source{i}.m -> sink.s; endfabric\n"
        source += "endmodule\n"
        pigen = directory / f"insufficient_{count}.pigen"
        pigen.write_text(source)
        output = run([ROOT / "pigen", pigen, "-o", directory / "bad.sv"], directory, expect=False)
        assert "target ID width must fit" in output, output
    print("PASS full AXI fabric rejects insufficient source-tag space for 2/3/5 initiators")


def hierarchy_negatives(directory):
    for family in ('axi4','axi4_stream'):
        options='addr=32,id=4' if family=='axi4' else 'id=4,dest=1,last=1'
        protocol=f'uint[32] {family}[{options}]'
        middle=protocol.replace('id=4','id=6')
        source=f'fabric first(input wire clk,input wire reset,input {protocol} s0,input {protocol} s1,output {middle} m) begin s0 -> m;s1 -> m;endfabric\n'
        source+='module bad(input wire clk,input wire reset,'+','.join(f'input {protocol} s{i}' for i in range(4))+f',output {middle} m0,output {middle} m1);\n'
        for group in range(2): source+=f'first g{group}(.clk(clk),.reset(reset),.s0(s{group*2}),.s1(s{group*2+1}));\n'
        first_window=' at [0:15]' if family=='axi4' else ''
        second_window=' at [16:31]' if family=='axi4' else ''
        source+=f'fabric begin g0.m -> m0{first_window};g1.m -> m0{first_window};g0.m -> m1{second_window};endfabric endmodule'
        path,output=directory/(family+'_tree_bad.pigen'),directory/(family+'_tree_bad.sv')
        path.write_text(source);output.write_text('preserved output')
        diagnostic=run([ROOT/'pigen',path,'-o',output,'--no-diagram'],directory,expect=False)
        assert ('target ID width must fit' if family=='axi4' else 'provenance bits') in diagnostic,diagnostic
        assert output.read_text()=='preserved output'
    print('PASS nested native fabric rejects insufficient cumulative source-ID space at the outer merge')


def main():
    root = Path(tempfile.mkdtemp(prefix="pigen-native-axi-fabric-"))
    try:
        negatives(root)
        hierarchy_negatives(root)
        for family, data, count, bare in [("axi4_lite", 32, 2, False), ("axi4_lite", 64, 3, False),
                                          ("axi4", 32, 2, False), ("axi4", 128, 3, False),
                                          ("axi4", 64, 3, True)]:
            directory = root / f"{family}_{data}_{count}_{bare}"
            directory.mkdir()
            case(directory, family, data, count, bare)
        for family in ('axi4_lite','axi4'):
            directory=root/('mapped_'+family);directory.mkdir()
            case(directory,family,64,3,boundary=True,mapped=True)
        for family in ('axi4_lite','axi4'):
            directory=root/('switched_'+family);directory.mkdir()
            case(directory,family,64,3,boundary=True,switched=True)
        for family,data,count,bare,exclusive in (
                ('axi4_lite',64,5,False,False),('axi4',64,5,False,False),
                ('axi4',32,7,True,False),('axi4',32,5,False,'shared'),
                ('axi4',32,7,True,'private')):
            directory=root/f'tree_{family}_{count}_{bare}_{exclusive}';directory.mkdir()
            case(directory,family,data,count,bare=bare,exclusive=exclusive,boundary=True,tree=True)
        for mode in ("private", "shared"):
            directory = root / f"exclusive_{mode}"
            directory.mkdir()
            case(directory, "axi4", 32, 3, exclusive=mode)
            directory = root / f"exclusive_bare_{mode}"
            directory.mkdir()
            case(directory, "axi4", 32, 3, bare=True, exclusive=mode)
        for family, data, count in [('axi4_lite', 32, 3), ('axi4', 64, 3)]:
            directory = root / f'boundary-{family}'
            directory.mkdir()
            case(directory, family, data, count, boundary=True)
        for family, data, count in [('axi4_lite', 64, 3), ('axi4', 128, 3)]:
            directory = root / f'standalone-{family}'
            directory.mkdir()
            case(directory, family, data, count, boundary=True, standalone=True)
        for family in ('axi4_lite','axi4'):
            directory = root / ('collection_fanin_'+family); directory.mkdir()
            case(directory, family, 32, 3, boundary=True, collections=True)
        directory = root / 'standalone-direct'
        directory.mkdir()
        case(directory, 'axi4', 32, 1, extra_id_bits=2, direct=True, boundary=True, standalone=True)
        for direct in (False, True):
            directory = root / f"single_{direct}"
            directory.mkdir()
            case(directory, "axi4", 32, 1, extra_id_bits=2, direct=direct)
    except BaseException:
        print(f"Native memory fabric evidence retained in {root}")
        raise
    else:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
