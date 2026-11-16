#!/usr/bin/env python3
"""Independent pin scoreboard for native packet-preserving fabric arbitration."""
from pathlib import Path
import shutil
import tempfile

from native_protocol_test import ROOT, run


def case(directory, count, has_last, boundary=False, standalone=False, actual_core=False, collections=False, metadata=False):
    fields = [("data", 32), ("keep", 4), ("strb", 4), ("id", 4),
              ("dest", 3), ("user", 7)] + ([("last", 1)] if has_last else [])
    config = "keep=1,strb=1,id=4,dest=3,user=7,clock=ACLK,resetn=ARESETn"
    if has_last:
        config += ",last=1"
    if actual_core:
        assert has_last and not standalone
        fields = [('data',32),('keep',4),('last',1)]
        config = 'keep=1,last=1,clock=ACLK,resetn=ARESETn'
    if metadata:
        assert not actual_core
        fields = [("id", 4), ("dest", 3), ("user", 32)] + ([("last", 1)] if has_last else [])
        config = "id=4,dest=3,user=32,clock=ACLK,resetn=ARESETn" + (",last=1" if has_last else "")
    protocol = f"{'void' if metadata else 'uint[32]'} axi4_stream[{config}]"
    controls = "input wire ACLK,input wire ARESETn"
    source = (f"module relay({controls},input {protocol} s,output {protocol} m);\n"
              "always @(posedge ACLK) if(ARESETn) m <= s;\nendmodule\n")
    if actual_core:
        source = (ROOT / 'cores/axis/fifo.pigen').read_text() + source
    ports = [f"input {protocol} s{i}" for i in range(count)] + [f"output {protocol} m"]
    if collections:
        assert boundary and not actual_core
        ports = [port + '[1]' for port in ports]
    source += f"{'fabric' if standalone else 'module'} native_merge({controls}," + ",".join(ports) + (") begin\n" if standalone else ");\n")
    if actual_core:
        source += 'pigen_axis_fifo sink(.clk(ACLK),.reset(!ARESETn),.m_axis(m));\n'
        for i in range(count):
            source += f'fabric link{i} begin s{i} -> sink.s_axis; endfabric\n'
    elif boundary:
        for i in range(count):
            source += f"s{i} -> m;\n" if standalone else f"fabric link{i} begin s{i} -> m; endfabric\n"
    else:
        source += "relay sink(.ACLK(ACLK),.ARESETn(ARESETn),.m(m));\n"
        for i in range(count):
            source += f"relay source{i}(.ACLK(ACLK),.ARESETn(ARESETn),.s(s{i}));\n"
            # Distinct blocks exercise cross-block destination grouping.
            source += f"fabric link{i} begin source{i}.m -> sink.s; endfabric\n"
    source += "endfabric\n" if standalone else "endmodule\n"
    pigen, sv, tb, binary = [directory / name for name in ("merge.pigen", "merge.sv", "tb.sv", "sim")]
    pigen.write_text(source)
    run([ROOT / "pigen", pigen, "-o", sv, "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", "native_merge", sv,
         ROOT / "rtl/pigen_primitives.sv"], directory)
    declarations, connections, drive, check = [], [".ACLK(clk)", ".ARESETn(!reset)"], [], []
    for endpoint in [f"s{i}" for i in range(count)] + ["m"]:
        for field, bits in fields + [("valid", 1), ("ready", 1)]:
            pin = f"{endpoint}_t{field}"
            declarations.append(f"logic [{bits-1}:0] {pin};")
            connections.append(f".{endpoint + '_0' if collections else endpoint}_t{field}({pin})")
    for i in range(count):
        drive += [f"if(reset) s{i}_tvalid=0; else if(!s{i}_tvalid || s{i}_tready) begin",
                  f"s{i}_tvalid=sent[{i}]<250;"]
        for index, (field, bits) in enumerate(fields):
            expr = f"{i}*65536+sent[{i}]" if field == ("user" if metadata else "data") else (
                f"(sent[{i}]%5)==4" if field == "last" else f"sent[{i}]*{index+3}+{i}*7")
            if has_last and field in ('id','dest'):
                # This witness demands rotation every five-beat packet, so
                # packet identity must remain constant through its TLAST.
                expr=f"(sent[{i}]/5)*{index+3}+{i}*7"
            drive.append(f"s{i}_t{field}={bits}'({expr});")
        drive.append("end")
        check.append(f"if(s{i}_tvalid && s{i}_tready) sent[{i}]=sent[{i}]+1;")
    packet = "{" + ",".join(f"m_t{field}" for field, _ in fields) + "}"
    width = sum(bits for _, bits in fields)
    carrier = "m_tuser" if metadata else "m_tdata"
    checks = [f"who=integer'({carrier}>>16); seq=integer'({carrier} & 32'hffff);",
              f"if(who<0 || who>={count}) $fatal(1,\"invalid source\");",
              "if(seq!=got[who] || seq>=sent[who]) $fatal(1,\"source order/loss\");"]
    for index, (field, bits) in enumerate(fields):
        if field != ("user" if metadata else "data"):
            expr = "(seq%5)==4" if field == "last" else f"seq*{index+3}+who*7"
            if has_last and field in ('id','dest'):
                expr=f"(seq/5)*{index+3}+who*7"
            checks.append(f'if(m_t{field} !== {bits}\'({expr})) $fatal(1,"sideband {field}");')
    if has_last:
        checks += ['if(packet_owner>=0 && packet_owner!=who) $fatal(1,"packet interleaved");',
                   "packet_owner=m_tlast ? -1 : who;"]
    checks += ["got[who]=got[who]+1; total=total+1; minimum=250; maximum=0;",
               f"for(integer i=0;i<{count};i++) begin if(got[i]<minimum) minimum=got[i]; if(got[i]>maximum) maximum=got[i]; end",
               'if(maximum-minimum>10) $fatal(1,"unfair fabric service");',
               f'if(epoch==1 && total=={count*250}) begin $display("PASS native packet fabric"); $finish; end']
    tb.write_text(
        "module tb; logic clk=0,reset=1; always #5 clk=~clk;\n"
        f"integer sent[0:{count-1}],got[0:{count-1}],seed=76543,epoch=0,total=0,who,seq,minimum,maximum,packet_owner=-1;\n"
        f"logic stalled=0; logic [{width-1}:0] held;\n" + "\n".join(declarations) +
        "\nnative_merge dut(" + ",".join(connections) + ");\n"
        "initial begin m_tready=0; repeat(4) @(negedge clk); #1; reset=0;\n"
        "repeat(45) @(negedge clk); #1; reset=1; repeat(3) @(negedge clk); #1; epoch=1; reset=0; end\n"
        "always @(negedge clk) begin m_tready=!reset && epoch==1 && (($random(seed)&7)>2);\n" +
        "\n".join(drive) + "\nend\n"
        f"always @(posedge clk) if(reset) begin for(integer i=0;i<{count};i++) begin sent[i]=0;got[i]=0;end total=0;stalled=0;packet_owner=-1;end else begin\n" +
        "\n".join(check) + f'\nif(stalled && (!m_tvalid || {packet} !== held)) $fatal(1,"unstable output");\n' +
        f"stalled=m_tvalid && !m_tready; held={packet};\n"
        "if(m_tvalid && m_tready) begin\n" + "\n".join(checks) + "\nend\nend\n"
        'initial begin #100000; $fatal(1,"fabric timeout"); end endmodule\n')
    run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, tb,
         ROOT / "rtl/pigen_primitives.sv"], directory)
    assert "PASS native packet fabric" in run(["vvp", binary], directory)
    print(f"PASS native AXI4-Stream fabric sources={count} last={has_last} real_fifo={actual_core} collections={collections}: {250*count} beats, sidebands, packet ownership, stalls and queued reset")


def metadata_fabric_tests(root):
    for has_last in (False, True):
        for mode in ('child', 'boundary', 'standalone'):
            directory = root / f'metadata_{has_last}_{mode}'
            directory.mkdir()
            case(directory, 3, has_last, boundary=mode != 'child', standalone=mode == 'standalone',
                 metadata=True)
    directory = root / 'metadata_collection'
    directory.mkdir()
    case(directory, 3, True, boundary=True, collections=True, metadata=True)


def negatives(directory):
    leaf = ("module relay(input wire clk,input wire reset,input uint[32] axi4_stream[id=4] s,"
            "output uint[32] axi4_stream[id=4] m);\n"
            "always @(posedge clk) if(!reset) m <= s;\nendmodule\n")
    wrong = leaf.replace("module relay(", "module wrong(").replace("id=4", "dest=4")
    cases = [
        ("relay", "clk", "reset", "", "a.s > b.m;", "initiator-to-target"),
        ("wrong", "clk", "reset", "", "a.m > b.s;", "compatible native"),
        ("relay", "other", "reset", "", "a.m -> b.s;", "clock domains"),
        ("relay", "clk", "altreset", "", "a.m -> b.s;", "reset bindings"),
        ("relay", "clk", "reset", "", "a.m -> b.s; a.m -> b.s;", "another fabric consumer"),
        ("relay", "clk", "reset", "", "a.m > b.s; c.m -> b.s;", "direct AXI fabric destination"),
        ("relay", "clk", "reset", "", "a.m -> b.s; c.m > b.s;", "direct AXI fabric destination"),
        ("relay", "clk", "reset", ",.m(m)", "a.m -> b.s;", "another transfer owner"),
    ]
    for i, (kind, clock, reset, binding, edges, diagnostic) in enumerate(cases):
        pigen = directory / f"bad{i}.pigen"
        pigen.write_text(leaf + wrong +
                         "module top(input wire clk,input wire reset,input wire other,input wire altreset,"
                         "output uint[32] axi4_stream[id=4] m);\n"
                         f"relay a(.clk({clock}),.reset({reset}){binding});\n"
                         f"{kind} b(.clk(clk),.reset(reset));\n"
                         "relay c(.clk(clk),.reset(reset));\n"
                         f"fabric routes begin {edges} endfabric\nendmodule\n")
        output = run([ROOT / "pigen", pigen, "-o", directory / "bad.sv"], directory, expect=False)
        assert diagnostic in output, f"fabric negative {i}: {output}"
    print("PASS native fabric contract, direction, timing and ownership rejection")


def main():
    root = Path(tempfile.mkdtemp(prefix="pigen-native-axis-fabric-"))
    try:
        metadata_fabric_tests(root)
        negatives(root)
        for count, has_last in [(2, True), (3, True), (5, True), (3, False)]:
            directory = root / f"{count}_{has_last}"
            directory.mkdir()
            case(directory, count, has_last)
            directory = root / f"boundary_{count}_{has_last}"
            directory.mkdir()
            case(directory, count, has_last, boundary=True)
        for has_last in (False, True):
            directory = root / f'standalone_{has_last}'
            directory.mkdir()
            case(directory, 3, has_last, boundary=True, standalone=True)
        directory = root / 'collection_fanin'; directory.mkdir()
        case(directory, 3, True, boundary=True, collections=True)
        for count in (2,3,5):
            directory = root / f'real_fifo_{count}'
            directory.mkdir()
            case(directory, count, True, actual_core=True)
    except BaseException:
        print(f"Native fabric evidence retained in {root}")
        raise
    else:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
