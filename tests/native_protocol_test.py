#!/usr/bin/env python3
"""Check native AXI declarations and complete channel records at standard pins."""
from pathlib import Path
import subprocess
import tempfile
import shutil
import re

ROOT = Path(__file__).resolve().parents[1]


def run(args, directory, expect=True):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    with (directory / "commands.log").open("a") as log:
        log.write("$ " + " ".join(map(str, args)) + "\n" + result.stdout)
    if (result.returncode == 0) != expect:
        raise AssertionError(result.stdout + f"\nCommand failed: {args}; log: {directory}")
    return result.stdout


def channels(family, data):
    # Independent pin specification; no compiler metadata or generated layout is read.
    if family == "axi4_stream":
        fields = [("data", data), ("strb", data // 8), ("keep", data // 8)] if data else []
        return {"t": fields + [("last", 1), ("id", 4), ("dest", 3), ("user", 7)]}
    if family == "axi4_lite":
        address = [("addr", 40), ("prot", 3)]
        return {"aw": address, "w": [("data", data), ("strb", data // 8)],
                "b": [("resp", 2)], "ar": address,
                "r": [("data", data), ("resp", 2)]}
    address = [("addr", 40), ("id", 4), ("len", 8), ("size", 3),
               ("burst", 2), ("lock", 1), ("cache", 4), ("prot", 3),
               ("qos", 4), ("region", 4)]
    return {"aw": address + [("user", 3)],
            "w": [("data", data), ("strb", data // 8), ("last", 1), ("user", 7)],
            "b": [("resp", 2), ("id", 4), ("user", 5)],
            "ar": address + [("user", 11)],
            "r": [("data", data), ("resp", 2), ("id", 4), ("last", 1), ("user", 13)]}


def ordinary_package_source(source, data, mode, signed=False):
    dtype = "int" if signed else "uint"
    source=source.replace(f"{dtype}[{data}]", "configuration::data_t")
    source=source.replace("addr=40", "addr=configuration::ADDRESS_BITS")
    source=source.replace("id=4", "id=configuration::ID_BITS")
    source=source.replace("fifo[3]", "fifo[configuration::DEPTH]")
    payload=(f"typedef bit {'signed ' if signed else ''}[{data-1}:0] data_t;")
    if mode=="record":
        assert data==32 and not signed
        payload="typedef struct packed {bit[15:0] hi; bit[15:0] lo;} data_t;"
    members=f"localparam int DATA_BITS={data},ADDRESS_BITS=40,ID_BITS=4,DEPTH=3; "+payload
    if mode in ("mixed","mixed_transitive"):
        members+=("function automatic data_t normalize(input data_t value);return value;endfunction\n"
                  "task automatic copy_word(input data_t value,output data_t result);result=value;endtask\n")
    if mode in ("transitive","mixed_transitive","retained_export","forward_call"):
        prefix=("package base_configuration; "+members+" endpackage\n"
                "package configuration; import base_configuration::*; "+
                ("function automatic int widths();return DATA_BITS+ADDRESS_BITS+ID_BITS+DEPTH;endfunction "
                 if mode=="retained_export" else "localparam USED=DATA_BITS+ADDRESS_BITS+ID_BITS+DEPTH; ")+"typedef data_t selected_t; "+
                ("import base_configuration::normalize,base_configuration::copy_word; " if mode=="mixed_transitive" else "")+
                "export *::*; endpackage\n")
    else:
        prefix="package configuration; "+members+" endpackage\n"
    if mode=="forward_call":
        prefix=("package base_configuration; "+members+
                "function automatic int compute(input int value);return value+7;endfunction endpackage\n"
                "package override_configuration;function automatic int compute(input int value);return value+13;endfunction endpackage\n"
                "package configuration;import base_configuration::*;"
                "function automatic int normalize(input int value);return `CONFIG_CALL(compute,value);endfunction "
                "import override_configuration::compute;"
                "function automatic int widths();return DATA_BITS+ADDRESS_BITS+ID_BITS+DEPTH;endfunction "
                "typedef data_t selected_t;export *::*;endpackage\n")
        prefix="`define CONFIG_CALL(name,argument) name(argument)\n"+prefix
    if mode in ("header","unit"):
        source=source.replace("configuration::", "")
        if mode=="header":
            source=re.sub(r"\bmodule (\w+)\s*",lambda m:f"module {m[1]} import configuration::*; ",source)
        else:
            prefix+="import configuration::*;\n"
    return prefix+source


PACKED_DATA_ALIASES = {
    "packed_vector": ("typedef uint[32] word_t;", "[2]"),
    "packed_signed": ("typedef int[32] word_t;", "[2]"),
    "packed_record": ("typedef struct packed {uint[16] hi;int[16] lo;} word_t;", "[2]"),
    "packed_chain": ("typedef uint[16] lane_t;typedef lane_t[2] word_t;", "[2]"),
    "packed_offset": ("typedef bit[23:8] word_t;", "[4]"),
    "packed_ascending": ("typedef bit[-3:12] word_t;", "[4]"),
    "packed_byte": ("typedef byte word_t;", "[8]"),
    "packed_shortint": ("typedef shortint word_t;", "[4]"),
    "packed_integer": ("typedef integer word_t;", "[2]"),
    "packed_longint": ("typedef longint word_t;", "[1]"),
    "packed_time": ("typedef time word_t;", "[1]"),
    "packed_multi": ("typedef bit[7:0] word_t;", "[2][4]"),
    "packed_range": ("typedef bit[15:0] word_t;", "[3:2][2:1]"),
}


def packed_data_alias_tests(root):
    for family, storage in (("axi4_stream", "buf"), ("axi4_lite", "fifo[3]"), ("axi4", "skid")):
        for mode in PACKED_DATA_ALIASES:
            for hierarchy in (False, 'fabric_routed'):
                directory = root / f"{mode}_{family}_{hierarchy}"
                directory.mkdir()
                case(directory, family, 64, storage, hierarchy=hierarchy, data_typedef=mode)


def foreign_axi_module(directory, family, data, storage, clock, reset, active_low, parameterized):
    ports = [f"input wire {clock}", f"input wire {reset}"]
    equations, processes = [], []
    released = reset if active_low else "!" + reset
    depth = 2 * {"buf": 1, "skid": 2, "fifo[3]": 3}[storage]
    for ch, fields in channels(family, data).items():
        incoming, outgoing = ("m", "s") if ch in ("b", "r") else ("s", "m")
        widths = [(name, "DATA_W" if name == "data" else "(DATA_W/8)" if name in ("strb", "keep") else "ADDR_W" if name == "addr" else "ID_W" if name == "id" else str(bits))
                  for name, bits in fields]
        for endpoint in ("s", "m"):
            direction = "input" if endpoint == incoming else "output"
            for field, width in widths:
                ports.append(f"{direction} wire [{width}-1:0] {endpoint}_{ch}{field}")
            ports.append(f"{direction} wire {endpoint}_{ch}valid")
            ports.append(f"{'output' if endpoint == incoming else 'input'} wire {endpoint}_{ch}ready")
        total = "+".join(width for _, width in widths)
        push = f"({incoming}_{ch}valid && {incoming}_{ch}ready)"
        pop = f"({outgoing}_{ch}valid && {outgoing}_{ch}ready)"
        inputs = "{" + ",".join(f"{incoming}_{ch}{field}" for field, _ in widths) + "}"
        outputs = "{" + ",".join(f"{outgoing}_{ch}{field}" for field, _ in widths) + "}"
        equations += [f"reg [{total}-1:0] queue_{ch}[0:{depth}-1];integer head_{ch},tail_{ch},count_{ch};",
                      f"assign {incoming}_{ch}ready=({released}) && count_{ch}<{depth};",
                      f"assign {outgoing}_{ch}valid=({released}) && count_{ch}!=0;",
                      f"assign {outputs}=queue_{ch}[head_{ch}];"]
        processes.append(f"always @(posedge {clock}) if(!({released})) begin head_{ch}<=0;tail_{ch}<=0;count_{ch}<=0;end else begin "
                         f"if({push}) begin queue_{ch}[tail_{ch}]<={inputs};tail_{ch}<=tail_{ch}=={depth-1}?0:tail_{ch}+1;end "
                         f"if({pop})head_{ch}<=head_{ch}=={depth-1}?0:head_{ch}+1;"
                         f"case ({{{push},{pop}}}) 2'b10:count_{ch}<=count_{ch}+1;2'b01:count_{ch}<=count_{ch}-1;default:;endcase end")
    ip = directory / "foreign_leaf.sv"
    ip.write_text(f"module native_leaf #(parameter integer DATA_W=32,ADDR_W={32 if parameterized else 40},ID_W={2 if parameterized else 4})(" + ",".join(ports) + ");\n" +
                  "\n".join(equations + processes) + "\nendmodule\n")
    return ip


def external_axi_tests(root):
    for family, storage in (("axi4_stream", "buf"), ("axi4_lite", "fifo[3]"), ("axi4", "skid")):
        for hierarchy in ('aggregate', True):
            for parameterized in (False, True):
                directory = root / f"external_{family}_{hierarchy}_{parameterized}"
                directory.mkdir()
                case(directory, family, 64 if parameterized else 32, storage, binding="low",
                     hierarchy=hierarchy, external="parameterized" if parameterized else "default")
        for hierarchy in ('aggregate', True):
            directory = root / f"external_generated_{family}_{hierarchy}"
            directory.mkdir()
            case(directory, family, 64, storage, binding="low", hierarchy=hierarchy,
                 external="generated_parameterized")
        source = (root / f"external_{family}_aggregate_False" / "relay.pigen").read_text()
        declaration, parent = source.split(";", 1)
        mutations = {"width": declaration.replace("uint[32]", "uint[64]"),
                     "role": declaration.replace("input uint[32]", "output uint[32]", 1),
                     "clock": declaration.replace("clock=aclk", "clock=aresetn")}
        if family == "axi4_stream":
            mutations["sideband"] = declaration.replace("last=1", "last=0")
        else:
            mutations["address"] = declaration.replace("addr=40", "addr=32")
        for name, contract in mutations.items():
            bad = root / f"external_bad_{family}_{name}.pigen"
            output = bad.with_suffix(".sv")
            bad.write_text(contract + ";" + parent)
            output.write_text("preserve output\n")
            result = run([ROOT / "pigen", bad, "-o", output, "--no-diagram"], root, expect=False)
            assert "AXI" in result or "native" in result or "clock" in result, result
            assert output.read_text() == "preserve output\n"
    print("PASS external AXI contracts: independent scalar-pin IP, defaults/overrides, direct/routed composition and negative contracts")



def qualified_if_tests(root):
    # Qualifiers retain native checks while each channel keeps its own fire.
    qualifiers = ("unique", "unique0", "priority")
    cases = [(f"plain_{storage}", f"uint[32] {storage}", "m", "s")
             for storage in ("buf", "fifo[3]", "skid")]
    cases += [(family, f"uint[32] {family}", "m" if family == "axi4_stream" else "m.w",
               "s" if family == "axi4_stream" else "s.w")
              for family in ("axi4_stream", "axi4_lite", "axi4")]
    for name, protocol, dst, src in cases:
        for qualifier in qualifiers:
            source = root / f"qualified_if_{name}_{qualifier}.pigen"
            sv = source.with_suffix(".sv")
            source.write_text(f"module relay(input wire clk,input wire reset,input wire gate,input wire other,"
                              f"input {protocol} s,output {protocol} m);always @(posedge clk)if(!reset)begin "
                              f"{qualifier} if(gate){dst}<={src};else if(other){dst}<={src};end endmodule")
            run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], root)
            assert f"{qualifier} if" in sv.read_text()
            run(["verilator", "--lint-only", "--top-module", "relay", sv,
                 ROOT / "rtl/pigen_primitives.sv"], root)
    for qualifier in qualifiers:
        source = root / f"ordinary_if_{qualifier}.pigen"
        sv = source.with_suffix(".sv")
        source.write_text("module relay(input wire clk,input wire reset,input wire gate,input wire other,"
                          "input wire[31:0] s,output logic[31:0] m);always @(posedge clk)if(!reset)begin "
                          f"{qualifier} if(gate)m<=s;else if(other)m<=s;end endmodule")
        run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], root)
        assert f"{qualifier} if" in sv.read_text()
        run(["verilator", "--lint-only", "--top-module", "relay", sv], root)
        tb = root / f"ordinary_if_{qualifier}_tb.sv"
        binary = root / f"ordinary_if_{qualifier}_sim"
        tb.write_text("module tb;logic clk=0,reset=1,gate=0,other=0;logic[31:0] s=0;wire[31:0] m;"
                      "integer mode=0,i;always #5 clk=~clk;relay dut(.*);initial begin "
                      "if(!$value$plusargs(\"MODE=%d\",mode))mode=0;repeat(3)@(negedge clk);reset=0;"
                      "for(i=0;i<256;i=i+1)begin gate=(i%2)!=0;other=!gate;s=32'h9e3779b9*32'(i+1);"
                      "@(posedge clk);#1;if(m!==s)$fatal(1,\"retained qualified branch mismatch\");@(negedge clk);end "
                      "if(mode==1)begin gate=0;other=0;end else if(mode==2)begin gate=1;other=1;end "
                      "@(posedge clk);#1;$display(\"PASS preserved qualified if branches and checks\");$finish;end endmodule")
        run(["verilator", "--binary", "--assert", "--timing", "--top-module", "tb", "-j", "2",
             "--Mdir", root / f"ordinary_if_{qualifier}_obj", "-o", binary, sv, tb], root)
        oracle = root / f"ordinary_if_{qualifier}_oracle.sv"
        oracle_binary = root / f"ordinary_if_{qualifier}_oracle_sim"
        oracle.write_text(source.read_text())
        run(["verilator", "--binary", "--assert", "--timing", "--top-module", "tb", "-j", "2",
             "--Mdir", root / f"ordinary_if_{qualifier}_oracle_obj", "-o", oracle_binary, oracle, tb], root)
        for mode in (0, 1, 2):
            # The installed simulator is also the native-SV qualifier oracle.
            reference = subprocess.run([oracle_binary, f"+MODE={mode}"], cwd=root, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
            with (root / "commands.log").open("a") as log:
                log.write(f"$ {oracle_binary} +MODE={mode} [native SV status={reference.returncode}]\n" + reference.stdout)
            expected = reference.returncode == 0
            assert expected if mode == 0 else True, reference.stdout
            assert "PASS preserved qualified if" in reference.stdout if expected else "Assertion failed" in reference.stdout, reference.stdout
            result = run([binary, f"+MODE={mode}"], root, expect=expected)
            assert "PASS preserved qualified if" in result if expected else "Assertion failed" in result, result
    print("PASS qualified if: 18 buffered/native declarations; three clocked native-SV branch/check oracles")


def qualified_if_combinational_tests(root):
    for qualifier in ("unique", "unique0", "priority"):
        for chained in (True, False):
            name=f"comb_if_{qualifier}_{chained}"
            source=root / (name+".pigen")
            sv=source.with_suffix(".sv")
            # Changing choose inside the entered branch must not reconsider
            # its selection. An explicit else block is not an else-if chain.
            second="if(other) begin m=s^32'h13579bdf; end else m=s;"
            branch=("else "+second) if chained else ("else begin "+second+" end")
            body=(f"{qualifier} if(choose) begin choose=0;m=s^32'h2468ace0;end "+branch)
            source.write_text("module relay(input wire gate,input wire other,input wire[31:0] s,"
                              "output logic[31:0] m);always_comb begin bit choose;"
                              "choose=bit[1]'(gate);"+body+" end endmodule")
            oracle=root / (name+"_oracle.sv")
            oracle.write_text("module relay(input wire gate,input wire other,input wire[31:0] s,"
                              "output logic[31:0] m);always_comb begin bit choose;"
                              "choose=gate;"+body+" end endmodule")
            tb=root / (name+"_tb.sv")
            tb.write_text("module tb;logic gate=0,other=0;logic[31:0] s=0;wire[31:0] m;integer i,mode;"
                          "relay dut(.*);initial begin if(!$value$plusargs(\"MODE=%d\",mode))mode=0;"
                          "for(i=0;i<256;i=i+1)begin gate=(i%2)!=0;other=!gate;s=32'h9e3779b9*32'(i+1);#1;"
                          "if(m!==(s^(gate ? 32'h2468ace0 : 32'h13579bdf)))$fatal(1,\"branch entry mismatch\");end "
                          "gate=(mode==2);other=(mode==2);#1;$display(\"PASS combinational qualified if\");$finish;end endmodule")
            run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],root)
            binaries=[]
            for tag,design in (("generated",sv),("native",oracle)):
                binary=root / (name+"_"+tag+"_sim")
                run(["verilator","--binary","--assert","--timing","--top-module","tb","-j","2",
                     "--Mdir",root/(name+"_"+tag+"_obj"),"-o",binary,design,tb],root)
                binaries.append(binary)
            for mode in (0,1,2):
                reference=subprocess.run([binaries[1],f"+MODE={mode}"],cwd=root,text=True,
                                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
                with (root/"commands.log").open("a") as log:log.write(reference.stdout)
                expected=reference.returncode==0
                if mode==0:assert expected,reference.stdout
                output=run([binaries[0],f"+MODE={mode}"],root,expect=expected)
                assert ("PASS combinational qualified if" in output) if expected else ("Assertion failed" in output),output
    print("PASS qualified combinational if: six chain/block native-SV oracles and branch-entry mutation")


def qualified_if_fsm_tests(root):
    for family in ("axi4_stream", "axi4_lite", "axi4"):
        for qualifier in ("unique", "unique0", "priority"):
            for storage, hierarchy in (("buf", False), ('fifo[3]', 'fabric_routed'),
                                       ("skid", "fabric_routed")):
                directory=root / f"qualified_fsm_{family}_{qualifier}_{storage}_{hierarchy}"
                directory.mkdir()
                case(directory, family, 32, storage, hierarchy=hierarchy, alias=True,
                     construction="fsm", qualifier=qualifier)


def qualified_if_fsm_oracles(root):
    ports=("input wire clk,input wire reset,input wire stop,input wire restart,"
           "input wire gate,input wire other,input wire jump,input wire[31:0] s,output logic[31:0] m")
    for qualifier in ("unique", "unique0", "priority"):
        body=(f"{qualifier} if(gate)begin m<=s;if(jump)goto idle;end else if(other)m<=s;"
              f"{qualifier} if(gate)m<=~s;else if(gate)m<=s;")
        source=root / f"fsm_checks_{qualifier}.pigen"
        sv=source.with_suffix(".sv")
        source.write_text(f"module relay({ports});fsm @(posedge clk)begin "
                          "if(reset)goto run;if(stop)goto idle;state run:begin "+body+
                          "end state idle:if(restart)goto run;end endmodule")
        # Independently implement the language's first-goto termination rule.
        reference_body=(f"{qualifier} if(gate)begin m<=s;if(jump)begin state<=IDLE;disable invocation;end end "
                        f"else if(other)m<=s;{qualifier} if(gate)m<=~s;else if(gate)m<=s;")
        oracle=root / f"fsm_checks_{qualifier}_oracle.sv"
        oracle.write_text(f"module relay({ports});localparam RUN=1'b0,IDLE=1'b1;logic state;"
                          "always @(posedge clk)begin : invocation "
                          "if(reset)begin state<=RUN;disable invocation;end "
                          "if(stop)begin state<=IDLE;disable invocation;end "
                          "if(state==RUN)begin "+reference_body+"end "
                          "if(state==IDLE && restart)begin state<=RUN;disable invocation;end end endmodule")
        tb=root / f"fsm_checks_{qualifier}_tb.sv"
        tb.write_text("module tb;logic clk=0,reset=1,stop=0,restart=0,gate=1,other=1,jump=1;"
                      "logic[31:0] s=0;wire[31:0] m;integer mode,i;always #5 clk=~clk;relay dut(.*);"
                      "initial begin if(!$value$plusargs(\"MODE=%d\",mode))mode=0;"
                      "repeat(3)@(negedge clk);reset=0;other=0;"
                      "for(i=0;i<128;i=i+1)begin s=32'h9e3779b9*32'(i+1);"
                      "@(posedge clk);#1;if(m!==s)$fatal(1,\"FSM prior-goto transfer mismatch\");"
                      "@(negedge clk);stop=1;gate=1;other=1;"
                      "@(posedge clk);#1;if(m!==s)$fatal(1,\"FSM stop did not terminate\");"
                      "@(negedge clk);stop=0;restart=1;"
                      "@(posedge clk);#1;if(m!==s)$fatal(1,\"inactive FSM state executed\");"
                      "@(negedge clk);restart=0;other=0;end "
                      "if(mode==1)begin gate=0;other=0;end "
                      "else if(mode==2)begin gate=1;other=1;end "
                      "else if(mode==3)jump=0;"
                      "@(posedge clk);#1;$display(\"PASS FSM qualifier termination and checks\");$finish;end endmodule")
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],root)
        binaries=[]
        for tag,design in (("generated",sv),("native",oracle)):
            binary=root / f"fsm_checks_{qualifier}_{tag}_sim"
            run(["verilator","--binary","--assert","--timing","--top-module","tb","-j","2",
                 "--Mdir",root/f"fsm_checks_{qualifier}_{tag}_obj","-o",binary,design,tb],root)
            binaries.append(binary)
        for mode in (0,1,2,3):
            reference=subprocess.run([binaries[1],f"+MODE={mode}"],cwd=root,text=True,
                                     stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            with (root/"commands.log").open("a") as log:log.write(reference.stdout)
            expected=reference.returncode==0
            if mode==0:assert expected,reference.stdout
            output=run([binaries[0],f"+MODE={mode}"],root,expect=expected)
            assert ("PASS FSM qualifier termination" in output) if expected else ("Assertion failed" in output),output
    print("PASS FSM qualified if: native-SV first-goto, reset, inactive-state and assertion parity")


def qualified_if_channel_tests(root):
    for family in ("axi4_stream", "axi4_lite", "axi4"):
        for qualifier in ("unique", "unique0", "priority"):
            for storage, hierarchy in (("buf", False), ('fifo[3]', 'fabric_routed'),
                                       ("skid", "fabric_routed")):
                directory=root / f"qualified_transport_{family}_{qualifier}_{storage}_{hierarchy}"
                directory.mkdir()
                case(directory, family, 32, storage, hierarchy=hierarchy, alias=True, qualifier=qualifier)


def four_state_channel_tests(root):
    for family in ("axi4_stream", "axi4_lite", "axi4"):
        for storage in ("buf", "fifo[3]", "skid"):
            for hierarchy in (False, 'fabric_routed'):
                for alias in (False, True):
                    directory = root / f"four_state_{family}_{storage}_{hierarchy}_{alias}"
                    directory.mkdir()
                    case(directory, family, 32, storage, hierarchy=hierarchy, alias=alias, four_state=True)
    for family in ("axi4_stream", "axi4"):
        for hierarchy in (False, 'fabric_routed'):
            directory = root / f"four_state_wide_{family}_{hierarchy}"
            directory.mkdir()
            case(directory, family, 128, "fifo[3]", hierarchy=hierarchy, alias=True, four_state=True)


def signed_four_state_channel_tests(root):
    for family, storage in (("axi4_stream", "buf"), ("axi4_lite", "fifo[3]"), ("axi4", "skid")):
        for hierarchy in (False, 'fabric_routed'):
            directory = root / f"signed_four_state_{family}_{hierarchy}"
            directory.mkdir()
            case(directory, family, 32, storage, hierarchy=hierarchy, alias=True, signed=True, four_state=True)
    for family in ("axi4_stream", "axi4"):
        for hierarchy in (False, 'fabric_routed'):
            directory = root / f"signed_four_state_wide_{family}_{hierarchy}"
            directory.mkdir()
            case(directory, family, 128, "fifo[3]", hierarchy=hierarchy, alias=True, signed=True, four_state=True)


def void_stream_token_tests(root):
    for storage in ("buf", "fifo[3]", "skid"):
        for routed in (False, True):
            directory = root / f"void_token_{storage}_{routed}"
            directory.mkdir()
            source, sv, tb, sim = [directory / name for name in ("token.pigen", "token.sv", "tb.sv", "sim")]
            body = ("fabric begin s -> m;endfabric" if routed else
                    "always @(posedge clk)if(!reset)m<=s;")
            source.write_text(f"typedef void axi4_stream token_t;module tokens(input wire clk,input wire reset,"
                              f"input token_t {storage} s,output token_t {storage} m);{body}\nendmodule")
            run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], directory)
            assert not re.search(r"\b[sm]_t(?:data|keep|strb|last|id|dest|user)\b", sv.read_text())
            run(["verilator", "--lint-only", "--top-module", "tokens", sv, ROOT / "rtl/pigen_primitives.sv"], directory)
            tb.write_text("module tb;reg clk=0,reset=1,s_tvalid=0,m_tready=0;wire s_tready,m_tvalid;"
                          "integer sent=0,got=0,epoch=0,seed=2187;always #5 clk=~clk;tokens dut(.*);"
                          "initial begin repeat(4)@(negedge clk);reset=0;repeat(100)@(negedge clk);"
                          "if(sent==0 || got!=0)$fatal(1,\"queued token reset witness\");"
                          "reset=1;s_tvalid=0;repeat(3)@(negedge clk);epoch=1;reset=0;end "
                          "always @(negedge clk)if(!reset)begin m_tready=epoch!=0 && (($random(seed)&7)>2);"
                          "if(!s_tvalid || s_tready)s_tvalid=sent<257 && (epoch==0 || (($random(seed)&3)!=0));end "
                          "always @(posedge clk)if(reset)begin sent=0;got=0;"
                          " #1;if(m_tvalid)$fatal(1,\"token reset isolation\");end else begin "
                          "if(s_tvalid && s_tready)sent=sent+1;if(m_tvalid && m_tready)begin "
                          "if(got>=sent)$fatal(1,\"phantom token\");got=got+1;end "
                          "if(epoch!=0 && got==257)begin #20;if(m_tvalid)$fatal(1,\"extra token\");"
                          "$display(\"PASS 257 handshake-only tokens, stalls and queued reset\");$finish;end end "
                          "initial begin #50000;$fatal(1,\"token timeout\");end endmodule")
            run(["iverilog", "-g2012", "-s", "tb", "-o", sim, sv, tb, ROOT / "rtl/pigen_primitives.sv"], directory)
            assert "PASS 257 handshake-only tokens" in run(["vvp", sim], directory)
    header = ("module bad(input wire clk,input wire reset,input void axi4_stream[last=1] s,"
              "output void axi4_stream[last=1] m);")
    negatives = [
        (header + "always @(posedge clk)if(!reset)m<=s.data;endmodule", "compatible expression"),
        (header.replace("[last=1]", "[keep=1]") + "endmodule", "invalid AXI protocol configuration"),
        (header.replace("[last=1]", "[strb=1]") + "endmodule", "invalid AXI protocol configuration"),
        (header.replace("axi4_stream[last=1]", "axi4_lite") + "endmodule", "invalid AXI protocol configuration"),
        (header.replace("axi4_stream[last=1]", "axi4") + "endmodule", "invalid AXI protocol configuration"),
        ("module bad(input void wire value);endmodule", "void DATA requires"),
        ("typedef struct packed {void absent;bit data;} bad_t;module bad();endmodule", "packed record members require integral"),
        ("typedef void empty_t;module bad();endmodule", "void DATA aliases require"),
    ]
    for index, (text, diagnostic) in enumerate(negatives):
        source = root / f"void_bad_{index}.pigen"
        sv = source.with_suffix(".sv")
        source.write_text(text);sv.write_text("preserve output\n")
        result = run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], root, expect=False)
        assert diagnostic in result, result
        assert sv.read_text() == "preserve output\n"
    print("PASS void Stream: six handshake-only profiles and eight preserved-output negatives")


def void_stream_tests(root):
    for storage in ("buf", "fifo[3]", "skid"):
        for hierarchy in (False, "aggregate"):
            for alias in (False, True):
                directory = root / f"void_stream_{storage}_{hierarchy}_{alias}"
                directory.mkdir()
                case(directory, "axi4_stream", 0, storage, hierarchy=hierarchy, alias=alias)
        directory = root / f"void_stream_external_{storage}"
        directory.mkdir()
        case(directory, "axi4_stream", 0, storage, binding="low", hierarchy=True, external="default")

    void_stream_token_tests(root)


def case(directory, family, data, storage="buf", signed=False, binding="default", hierarchy=False, construction=False, fabric=False, alias=False, ordinary_package=False, data_typedef=False, external=False, four_state=False, qualifier=None, case_control=None):
    channel_fields = channels(family, data)
    config = {"axi4_stream": "keep=1,strb=1,last=1,id=4,dest=3,user=7",
              "axi4_lite": "addr=40",
              "axi4": "addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13"}[family]
    numerical = "int" if signed else "uint"
    data_type = f"{numerical}[{data}]" if data else "void"
    if four_state:
        assert data
        data_type = f"logic {'signed ' if signed else ''}[{data-1}:0]"
    if not data:
        assert family == "axi4_stream" and not signed
        config = config.replace("keep=1,strb=1,", "")
    alias_contract = f"{data_type} {family}[{config}]"
    clock_name = "clk" if binding == "default" else "aclk"
    reset_name = "reset" if binding == "default" else "areset" if binding == "high" else "aresetn"
    active_low = binding not in ("default", "high")
    if binding != "default":
        config += f",clock={clock_name},{'resetn' if active_low else 'reset'}={reset_name}"
    protocol = f"{data_type} {family}[{config}] {storage}"
    control_ports = [f"input wire {clock_name}", f"input wire {reset_name}"]
    ports = [f"input {protocol} s", f"output {protocol} m"]
    ports = ports + control_ports if binding == "late_low" else control_ports + ports
    transfers, observations, observed_pins, record_types = [], [], [], []
    for ch, fields in channel_fields.items():
        incoming, outgoing = ("m", "s") if ch in ("b", "r") else ("s", "m")
        selection = "beat" if ch == "t" else ch
        value = f"{incoming}.{selection}"
        if str(construction).startswith('inquiry_'):
            control='peek' if 'peek' in construction else 'valid' if 'valid' in construction else 'ready' if 'ready' in construction else 'accepts'
            argument=f'peek({incoming}.{selection}.{fields[0][0]})[0]' if control=='peek' else f'valid({incoming}.{selection})' if control=='valid' else f'ready({outgoing}.{selection})' if control=='ready' else f'accepts({outgoing}.{selection},{incoming}.{selection})'
            selector=f'({argument} ? 32\'d1 : 32\'d0)'
            expected=f'({argument} ? 32\'d{sum(width for _,width in fields)} : 32\'bx)'
            checked=f'($size({incoming}.{selection},{selector}) === {expected})'
            members=[]
            for field,bits in fields:
                width=f'($bits(peek({incoming}.{selection}).{field})+$bits(valid({incoming}.{selection}))+$bits(ready({outgoing}.{selection}))+$bits(accepts({outgoing}.{selection},{incoming}.{selection}))-3)'
                raw=f'{incoming}.{selection}.{field}'
                members.append(f"{field}:{numerical if field=='data' else 'uint'}[{width}]'({raw} ^ ({checked} ? {bits}'d0 : ~{bits}'d0))")
            value="'{"+','.join(members)+'}'
        if str(construction).startswith('query_'):
            query_name=f'query_{ch}'
            ports.append(f'output logic[31:0] wire {query_name}')
            observations.append(f"{'assign' if construction=='query_assign' else 'always_comb'} {query_name}=$size({incoming}.{selection}.{fields[0][0]},({reset_name} ? 32'd0 : 32'd1));")
            observed_pins.append((query_name,32,f"({'!reset' if active_low else 'reset'} ? 32'bx : 32'd{fields[0][1]})"))
        if construction:
            chosen = fields[:1] if construction == "default" else fields[1:] + fields[:1]
            members = [f"{field}: {incoming}.{selection}.{field}" for field, _ in chosen]
            if construction == "default":
                members.append("default: 0")
            value = "'{" + ",".join(members) + "}"
        if str(construction).startswith('fill_'):
            fill="'x" if construction in ('fill_x','fill_cast_x') else "'z" if construction=='fill_z' else f"({incoming}.{selection}.{fields[-1][0]}[0] ? ('1) : '0)" if construction in ('fill_conditional','fill_cast_conditional') else "('1)"
            value="'{"+f'{fields[-1][0]}:{incoming}.{selection}.{fields[-1][0]},default:{fill}'+'}'
            if str(construction).startswith('fill_cast_'):
                value="'{"+','.join(f"{field}:"+(f'{incoming}.{selection}.{field}' if field==fields[-1][0] else f"{numerical if field=='data' else 'uint'}[{bits}]'({fill})") for field,bits in fields)+'}'
        if str(construction).startswith('compound_fill'):
            value="'{"+','.join(f"{field}:{numerical if field=='data' else 'uint'}[{bits}]'('1 ^ ~bit[{bits}]'({incoming}.{selection}.{field}))" for field,bits in fields)+'}'
        if str(construction).startswith('selected_fill'):
            value="'{"+','.join(f"{field}:"+(f'{incoming}.{selection}.{field}' if bits==1 else f"{numerical if field=='data' else 'uint'}[{bits}]'((('1 ^ ~bit[{bits}]'({incoming}.{selection}.{field})))[('1 & 32'd{bits-1}):0])") for field,bits in fields)+'}'
        if str(construction).startswith('called_fill'):
            members=[]
            for index,(field,bits) in enumerate(fields):
                raw=f"bit[{bits}]'({incoming}.{selection}.{field})"
                if construction=='called_fill_peek' and index: raw=f'peek({raw})'
                members.append(f"{field}:{numerical if field=='data' else 'uint'}[{bits}]'($unsigned('1 ^ ~{raw}))")
            value="'{"+','.join(members)+'}'
        if str(construction).startswith('query_'):
            members=[]
            payload_bits=sum(width for _,width in fields)
            for field,bits in fields:
                raw=f'{incoming}.{selection}.{field}'
                selector=f'({raw}[0] ? 32\'d1 : 32\'d0)'
                expected=f'({raw}[0] ? 32\'d{payload_bits} : 32\'bx)'
                checked=f'($size({incoming}.{selection},{selector}) === {expected})'
                members.append(f"{field}:{numerical if field=='data' else 'uint'}[{bits}]'({raw} ^ ({checked} ? {bits}'d0 : ~{bits}'d0))")
            value="'{"+','.join(members)+'}'
        if str(construction).startswith('bits_'):
            value="'{"+','.join(f"{field}:{numerical if field=='data' else 'uint'}[$bits({incoming}.{selection}.{field})]'({incoming}.{selection}.{field})" for field,_ in fields)+'}'
        if str(construction).startswith('zero_'):
            members=[]
            for field,bits in fields:
                raw=f'{incoming}.{selection}.{field}'
                packed='{{0{'+raw+'}},'+raw+',{0{peek('+raw+')}}}'
                members.append(f"{field}:{numerical if field=='data' else 'uint'}[{bits}]'({packed})")
            value="'{"+','.join(members)+'}'
        if construction in ('conditional', 'conditional_mixed', 'conditional_nested', 'conditional_when', 'conditional_fsm'):
            selector = f'{incoming}.{selection}.{fields[0][0]}[0]'
            alternative = "'{" + ','.join(f'{field}:{incoming}.{selection}.{field}' for field, _ in fields) + '}'
            if construction == 'conditional_mixed': alternative = f'{incoming}.{selection}'
            if construction == 'conditional_nested': alternative = f'({selector} ? {alternative} : {incoming}.{selection})'
            value = f'({selector} ? ({value}) : ({alternative}))'
        if str(construction).startswith('cast'):
            typename = 'record_' + ch
            declarations = [f'bit {"signed " if signed and field == "data" else ""}[{bits-1}:0] {field};' for field, bits in reversed(fields)]
            record_types.append('typedef struct packed {' + ''.join(declarations) + '} ' + typename + ';')
            cast_value = typename + "'(" + value + ')'
            if construction == 'cast_conditional':
                selector = f'{incoming}.{selection}.{fields[0][0]}[0]'
                cast_value = typename + "'(" + selector + ' ? ' + value + ' : ' + value + ')'
            if construction == 'cast_concat': cast_value = '{' + cast_value + '}'
            if construction == 'cast_replication': cast_value = '{2{' + cast_value + '}}'
            if construction == 'cast_bitwise': cast_value += " ^ " + typename + "'('{default:0})"
            value = f"uint[{sum(bits for _, bits in fields)}]\'({cast_value})"
            if construction == 'cast_projected':
                value = "'{" + ','.join(f"{field}:{numerical if field=='data' else 'uint'}[{bits}]'(({cast_value}).{field})" for field,bits in fields) + '}'
        if construction == 'peek_projected':
            value = "'{" + ','.join(f"{field}:" + (f'{incoming}.{selection}.{field}' if index==0 else f'peek({incoming}.{selection}).{field}') for index,(field,_) in enumerate(fields)) + '}'
        if construction == "procedural_import":
            value = "'{" + ','.join(f"{field}:{numerical if field=='data' else 'uint'}[BITS_{ch}_{field}]'({incoming}.{selection}.{field})" for field,_ in fields) + '}'
        if construction == "procedural_import_type":
            value = "'{" + ','.join(f"{field}:type_{ch}_{field}'({incoming}.{selection}.{field})" for field,_ in fields) + '}'
        if data_typedef in ("inline","inline_macro","inline_retained","inline_mutable") and construction == "nested_data":
            raw=f"{incoming}.{selection}.data"
            nested="'{hi:"+raw+".hi,low:'{tag:"+raw+".low.tag,value:"+raw+".low.value}}"
            value="'{"+','.join(f"{field}:"+(nested if field=="data" else f"{incoming}.{selection}.{field}") for field,_ in fields)+'}'
        assignment = f"{outgoing}.{selection} <= {value}"
        transfers.append(f"when ({assignment}) begin end" if construction in ("inquiry_peek_when","inquiry_peek_fsm","inquiry_valid_when","inquiry_valid_fsm","inquiry_ready_when","inquiry_ready_fsm","inquiry_accepts_when","inquiry_accepts_fsm","query_when", "query_fsm", "bits_when", "bits_fsm", "zero_when", "zero_fsm", "when", "fsm", "fill_when", "fill_fsm", "compound_fill_when", "compound_fill_fsm", "selected_fill_when", "selected_fill_fsm", "called_fill_when", "called_fill_fsm", "conditional_when", "conditional_fsm", "cast_when", "cast_fsm") else assignment + ";")
        if case_control:
            control=(f"{incoming}.{selection}.{fields[0][0]}[0]" if case_control.endswith("_direct") else
                     f"peek({incoming}.{selection}.{fields[0][0]})[0]")
            kind=case_control.removesuffix("_direct")
            transfer=transfers[-1]
            transfers[-1]=(f"{qualifier+' ' if qualifier else ''}{kind}({control}) "
                           f"default:begin {transfer} end 1'b0,1'b1:begin {transfer} end endcase")
        elif qualifier:
            control=f"peek({incoming}.{selection}.{fields[0][0]})[0]"
            transfer=transfers[-1]
            transfers[-1]=(f"{qualifier} if({control}) begin {transfer} end "
                           f"else if(!({control})) begin {transfer} end else begin {transfer} end")
        for field, bits in fields:
            name = f"seen_{ch}_{field}"
            dtype = "logic" if four_state else numerical if field == "data" else "uint"
            ports.append(f"output {dtype}[{bits}] wire {name}")
            if str(construction).startswith('bits_'):
                observations.append(f"always_comb {name} = {dtype}[{bits}]'($bits({incoming}.{selection}.{field}) == {bits} ? peek({outgoing}.{selection}.{field}) : 0);")
            else:
                observations.append(f"assign {name} = peek({outgoing}.{selection}.{field});")
            observed_pins.append((name, bits, f"{outgoing}_{ch}{field}"))
            if four_state and signed and field == "data":
                sign_probe = f"signed_{ch}_negative"
                slice_probe = f"signed_{ch}_byte"
                ports += [f"output logic wire {sign_probe}", f"output logic[7:0] wire {slice_probe}"]
                observations += [f"assign {sign_probe}=peek({outgoing}.{selection}.data)<0;",
                                 f"assign {slice_probe}=peek({outgoing}.{selection}.data)[7:0];"]
                observed_pins += [(sign_probe, 1, f"($signed({outgoing}_{ch}data)<0)"),
                                  (slice_probe, 8, f"{outgoing}_{ch}data[7:0]")]

            if str(data_typedef).startswith("packed_") and field == "data":
                projection, expected = {
                    "packed_vector": ("[1][15:8]", "[47:40]"),
                    "packed_signed": ("[1][15:8]", "[47:40]"),
                    "packed_record": ("[1].hi[15:8]", "[63:56]"),
                    "packed_chain": ("[1][0][15:8]", "[47:40]"),
                    "packed_offset": ("[2][23:16]", "[47:40]"),
                    "packed_ascending": ("[2][-3:4]", "[47:40]"),
                    "packed_byte": ("[7][7:0]", "[63:56]"),
                    "packed_shortint": ("[3][15:8]", "[63:56]"),
                    "packed_integer": ("[1][15:8]", "[47:40]"),
                    "packed_longint": ("[0][15:8]", "[15:8]"),
                    "packed_time": ("[0][15:8]", "[15:8]"),
                    "packed_multi": ("[1][2][7:0]", "[55:48]"),
                    "packed_range": ("[2][1][15:8]", "[15:8]"),
                }[data_typedef]
                probe = f"packed_{ch}_byte"
                ports.append(f"output logic[7:0] wire {probe}")
                observations.append(f"assign {probe}=peek({outgoing}.{selection}.data){projection};")
                observed_pins.append((probe, 8, f"{outgoing}_{ch}data{expected}"))
            if data_typedef in ("inline","inline_macro","inline_retained","inline_mutable") and field == "data":
                record_base=f"peek({outgoing}.{selection}.data)"
                if data_typedef=="inline_retained":
                    record_base=f"snapshot_{ch}"
                    observations.append(f"word_t wire {record_base};assign {record_base}=peek({outgoing}.{selection}.data);")
                    observations.append(f"function automatic bit sign_{ch}(input word_t value);return value.low.value<0;endfunction")
                for suffix,width,expression,expected in [
                    ("negative",1,f"{record_base}.low.value<0",f"{outgoing}_{ch}data[7]"),
                    ("tag",8,f"{record_base}.low.tag",f"{outgoing}_{ch}data[15:8]"),
                    ("hi",16,f"{record_base}.hi",f"{outgoing}_{ch}data[31:16]")]:
                    observed=f"record_{ch}_{suffix}"
                    ports.append(f"output bit[{width}] wire {observed}")
                    observations.append(f"assign {observed}={expression};")
                    observed_pins.append((observed,width,expected))
                if data_typedef=="inline_retained":
                    observed=f"helper_{ch}_negative"
                    ports.append(f"output bit wire {observed}")
                    observations.append(f"assign {observed}=sign_{ch}({record_base});")
                    observed_pins.append((observed,1,f"{outgoing}_{ch}data[7]"))
                if data_typedef=="inline_mutable":
                    observed=f"mutated_{ch}"
                    ports.append(f"output uint[32] wire {observed}")
                    observations.append(f"always_comb begin word_t temp_{ch};int[5] index_{ch};"
                                        f"temp_{ch}=peek({outgoing}.{selection}.data);"
                                        f"index_{ch}=int[5]'(temp_{ch}.low.tag[2:0])-4;"
                                        f"temp_{ch}.low.value[index_{ch}]=~temp_{ch}.low.value[index_{ch}];"
                                        f"{observed}=temp_{ch};end")
                    pin=f"{outgoing}_{ch}data"
                    observed_pins.append((observed,32,f"({pin} ^ (32'h1 << {pin}[10:8]))"))
                if data_typedef=="inline_macro":
                    observed=f"macro_record_{ch}_negative"
                    ports.append(f"output bit wire {observed}")
                    observations.append(f"assign {observed}=(other_t'(peek({outgoing}.{selection}.data))).different.value<0;")
                    observed_pins.append((observed,1,f"{outgoing}_{ch}data[23]"))
    if signed and not four_state:
        ports.append("output bit wire negative")
        observations.append("assign negative = peek(m.data) < 0;")
        ports.append("output bit[8] wire low_byte")
        observations.append("assign low_byte = peek(m.data[7:0]);")
    source = "module native_relay (\n" + ",\n".join(ports) + ");\n"
    source += "\n".join(record_types) + "\n" + "\n".join(observations)
    reset_released = reset_name if active_low else "!" + reset_name
    if fabric:
        source += "\nfabric begin s " + (">" if fabric == "direct" else "->") + " m; endfabric\nendmodule\n"
    elif construction in ("inquiry_peek_fsm","inquiry_valid_fsm","inquiry_ready_fsm","inquiry_accepts_fsm","query_fsm", "bits_fsm", "zero_fsm", "fsm", "fill_fsm", "compound_fill_fsm", "selected_fill_fsm", "called_fill_fsm", "conditional_fsm", "cast_fsm"):
        source += f"\nfsm @(posedge {clock_name}) begin\nif (!({reset_released})) goto transfer;\nstate transfer: begin\n"
        source += "\n".join(transfers) + "\nend\nend\nendmodule\n"
    else:
        source += f"\nalways @(posedge {clock_name}) if ({reset_released}) begin\n" + "\n".join(transfers) + "\nend\nendmodule\n"
    if str(construction).startswith("procedural_import"):
        widths = ''.join(f'localparam BITS_{ch}_{field}={bits};' for ch,fields in channel_fields.items() for field,bits in fields)
        types = ''.join(f'typedef {numerical if field=="data" else "uint"}[{bits}] type_{ch}_{field};' for ch,fields in channel_fields.items() for field,bits in fields)
        source = 'package channel_sizes; typedef uint[32] axi4_stream marker_t;' + widths + types + 'endpackage\n' + source
        source = source.replace(f'if ({reset_released}) begin\n',f'if ({reset_released}) begin\nimport channel_sizes::*;\n')
    if hierarchy:
        leaf = source.replace("module native_relay (", "module native_leaf (")
        if external:
            width = "DATA_W" if "parameterized" in str(external) else str(data)
            parameters = " #(parameter uint[32] DATA_W=32,ADDR_W=32,ID_W=2)" if "parameterized" in str(external) else ""
            contract_config = config.replace("addr=40", "addr=ADDR_W").replace("id=4", "id=ID_W") if "parameterized" in str(external) else config
            external_data = f"uint[{width}]" if data else "void"
            leaf = (f"extern module native_leaf{parameters}({','.join(control_ports)},"
                    f"input {external_data} {family}[{contract_config}] s,output {external_data} {family}[{contract_config}] m);\n")
        if hierarchy == "inverted":
            assert active_low
            leaf = leaf.replace(f"resetn={reset_name}", f"reset={reset_name}")
            leaf = leaf.replace(f"if ({reset_name})", f"if (!{reset_name})")
        parent_transfers = []
        for ch in channel_fields:
            selection = "beat" if ch == "t" else ch
            if ch in ("b", "r"):
                parent_transfers += [f"leaf.m.{selection} <= m.{selection};",
                                     f"s.{selection} <= leaf.s.{selection};"]
            else:
                parent_transfers += [f"leaf.s.{selection} <= s.{selection};",
                                     f"m.{selection} <= leaf.m.{selection};"]
        source = leaf + "module native_relay (\n" + ",\n".join(ports) + ");\n"
        controls = f".{clock_name}({clock_name}),.{reset_name}({reset_name})"
        if hierarchy == "inverted":
            source += f"native_leaf leaf(.{clock_name}({clock_name}),.{reset_name}(!{reset_name}),.s(s),.m(m));\n"
        elif hierarchy == "aggregate":
            source += f"native_leaf leaf({controls},.s(s),.m(m));\n"
        elif hierarchy == "positional":
            source += f"native_leaf leaf({clock_name},{reset_name},s,m);\n"
        elif hierarchy == "siblings":
            source += f"native_leaf second({controls},.s(first.m),.m(m));\n"
            source += f"native_leaf first({controls},.s(s));\n"
        elif hierarchy in ("fabric_direct", "fabric_routed"):
            source += f"native_leaf second({controls},.m(m));\n"
            source += f"native_leaf first({controls},.s(s));\n"
            arrow = ">" if hierarchy == "fabric_direct" else "->"
            source += f"fabric native_link begin first.m {arrow} second.s; endfabric\n"
        else:
            source += f"native_leaf leaf({controls});\n"
        source += "\n".join(observations)
        if hierarchy is True:
            source += f"\nalways @(posedge {clock_name}) if ({reset_released}) begin\n"
            source += "\n".join(parent_transfers) + "\nend\n"
        source += "endmodule\n"
    foreign_sources = []
    if external:
        if "parameterized" in str(external):
            source = source.replace("native_leaf leaf(", f"native_leaf #(.DATA_W({data}),.ADDR_W(40),.ID_W(4)) leaf(")
        foreign_sources = [foreign_axi_module(directory, family, data, storage, clock_name, reset_name, active_low,
                                              "parameterized" in str(external))]
        if str(external).startswith("generated"):
            source, count = re.subn(r"(native_leaf(?: #\([^;]*?\))? leaf\([^;]*\);)",
                                    r"if(1) begin : foreign_scope \1 end", source, count=1)
            assert count == 1
            if hierarchy is True:
                source=source.replace("leaf.s.","foreign_scope.leaf.s.").replace("leaf.m.","foreign_scope.leaf.m.")

    if alias:
        timing = "" if binding == "default" else f"[clock={clock_name},{'resetn' if active_low else 'reset'}={reset_name}]"
        source = (f"typedef {alias_contract} native_bus_t;\n"
                  "typedef native_bus_t copied_bus_t;\n" +
                  source.replace(protocol, f"copied_bus_t{timing} {storage}"))
        if str(alias).startswith("package"):
            source = source[source.index("module "):].replace("copied_bus_t", "bus_defs::copied_bus_t")
            contract = alias_contract.replace(f"{numerical}[{data}]", f"{numerical}[DATA_BITS]").replace("addr=40", "addr=ADDRESS_BITS")
            source = (f"package bus_defs; localparam int DATA_BITS={data}; localparam int ADDRESS_BITS=40; "
                      f"typedef {contract} native_bus_t; typedef native_bus_t copied_bus_t; endpackage\n" + source)
        if "package_subroutine" in str(alias):
            subroutine="function automatic logic[DATA_BITS-1:0] normalize(input logic[DATA_BITS-1:0] value);return value;endfunction\n"
            source=source.replace("endpackage\n",subroutine+"endpackage\n",1)
        if "package_export" in str(alias):
            if "early" in str(alias):
                body="export bus_defs::copied_bus_t; typedef copied_bus_t view_t; import bus_defs::*;"
            elif "all" in str(alias):
                body="export *::*; import bus_defs::copied_bus_t;"
            elif "wildcard" in str(alias):
                body="import bus_defs::*; typedef copied_bus_t witness_t; export bus_defs::*;"
            else:
                body="import bus_defs::*; export bus_defs::copied_bus_t;"
            source=source.replace("endpackage\n",f"endpackage\npackage exported_buses; {body} endpackage\n",1)
            source=source.replace("bus_defs::copied_bus_t"+timing,"exported_buses::copied_bus_t"+timing)
            if "early" in str(alias):
                source=source.replace("exported_buses::copied_bus_t"+timing,"exported_buses::view_t"+timing)
        if "parameterized" in str(alias):
            source = source.replace("module native_leaf (", "module native_leaf #(parameter int DEPTH=2) (")
            source = source.replace("native_leaf leaf(", "native_leaf #(.DEPTH(3)) leaf(")
        if alias == "parameterized_default":
            source = source.replace(f"copied_bus_t{timing} {storage}", f"copied_bus_t{timing}")
        if "package_import" in str(alias):
            explicit = "explicit" in str(alias)
            entry = "bus_defs::copied_bus_t" if explicit else "bus_defs::*"
            if "body" in str(alias):
                source = re.sub(r"(module \w+[^;]*;)", r"\1\nimport bus_defs::*; typedef copied_bus_t local_bus_t;", source)
            elif "package_import_package" in str(alias):
                source = source.replace("endpackage\n", "endpackage\npackage imported_buses; import bus_defs::*; typedef copied_bus_t shared_t; endpackage\n", 1)
                source = source.replace("bus_defs::copied_bus_t", "imported_buses::shared_t")
            else:
                source = source.replace("bus_defs::copied_bus_t", "copied_bus_t")
                if "header" in str(alias):
                    source = re.sub(r"\bmodule (\w+)\s*", lambda m: f"module {m[1]} import {entry}; ", source)
                else:
                    source = source.replace("endpackage\n", f"endpackage\nimport {entry};\n", 1)
    if ordinary_package:
        source=ordinary_package_source(source,data,ordinary_package,signed)
    if str(data_typedef).startswith("packed_"):
        assert data == 64
        definition, dimensions = PACKED_DATA_ALIASES[data_typedef]
        source = definition + "\n" + source.replace("uint[64]", "word_t" + dimensions)
        if data_typedef in ("packed_integer", "packed_time"):
            source = re.sub(r"output uint\[(\d+)\] wire seen_",
                            lambda m: f"output logic[{int(m[1])-1}:0] wire seen_", source)
    elif data_typedef:
        source=source.replace("uint[32]", "word_t")
        record=("typedef struct packed {uint[16] hi;struct packed {uint[8] tag;int[8] value;} low;} word_t;"
                if data_typedef in ("inline","inline_macro","inline_retained","inline_mutable") else "typedef struct packed {uint[16] hi;int[16] lo;} word_t;")
        if data_typedef=="inline_mutable":
            record=record.replace("uint[8] tag;int[8] value;","bit[7:0] tag;bit signed[3:-4] value;")
        if data_typedef=="inline_macro":
            other="typedef struct packed {uint[8] tag;struct packed {int[16] value;uint[8] endtag;} different;} other_t;"
            native=f"typedef word_t {family}[{config}] macro_bus_t;"
            source=source.replace(f"word_t {family}[{config}]", "macro_bus_t")
            source="`define DECLARE_RECORDS "+record+other+native+"\n`DECLARE_RECORDS\n"+source
        else:
            source=record+"\n"+source
    pigen = directory / "relay.pigen"
    pigen.write_text(source)
    sv = directory / "relay.sv"
    run([ROOT / "pigen", pigen, "-o", sv], directory)
    if not data:
        assert not re.search(r"\b[sm]_t(?:data|keep|strb)\b", sv.read_text()), "void Stream emitted DATA/byte qualifier pins"
    if str(construction).startswith("procedural_import"):
        assert 'import channel_sizes' not in sv.read_text(), "procedural imports leaked into module scope"
    if data_typedef == "packed_ascending":
        oracle = directory / "ascending_oracle.sv"
        oracle.write_text("typedef bit[-3:12] word_t;module native_relay(input word_t[3:0] s,output word_t[3:0] m);assign m=s;endmodule")
        for candidate in (oracle, sv):
            lint = run(["verilator", "--lint-only", "--top-module", "native_relay", candidate,
                        ROOT / "rtl/pigen_primitives.sv"], directory, expect=False)
            assert set(re.findall(r"%Warning-([A-Z0-9_]+)", lint)) == {"ASCRANGE"}, lint
    else:
        run(["verilator", "--lint-only", "--top-module", "native_relay", sv,
             ROOT / "rtl/pigen_primitives.sv", *foreign_sources], directory)

    connections = [f".{clock_name}(clk)", f".{reset_name}({'!reset' if active_low else 'reset'})"]
    declarations, drive, check, init, done = [], [], [], [], []
    physical_outputs, reset_scoreboard, capacity_checks = [], [], []
    physical_output_bits = 0
    for ch, fields in channel_fields.items():
        incoming, outgoing = ("m", "s") if ch in ("b", "r") else ("s", "m")
        total = sum(bits for _, bits in fields)
        for prefix in ("s", "m"):
            for field, bits in fields:
                pin = f"{prefix}_{ch}{field}"
                declarations.append(f"logic [{bits-1}:0] {pin};")
                connections.append(f".{pin}({pin})")
            for signal in ("valid", "ready"):
                pin = f"{prefix}_{ch}{signal}"
                declarations.append(f"logic {pin};")
                connections.append(f".{pin}({pin})")
        in_packet = "{" + ",".join(f"{incoming}_{ch}{f}" for f, _ in fields) + "}"
        out_packet = "{" + ",".join(f"{outgoing}_{ch}{f}" for f, _ in fields) + "}"
        iv, ir, ov, ready = (f"{incoming}_{ch}valid", f"{incoming}_{ch}ready",
                             f"{outgoing}_{ch}valid", f"{outgoing}_{ch}ready")
        declarations += [f"logic [{total-1}:0] expected_{ch}[0:256], held_{ch};",
                         f"integer sent_{ch}=0, got_{ch}=0; logic stalled_{ch}=0;"]
        init += [f"{iv}=0; {ready}=0;"] + [f"{incoming}_{ch}{f}=0;" for f, _ in fields]
        physical_outputs += [ir, ov] + [f"{outgoing}_{ch}{f}" for f, _ in fields]
        physical_output_bits += 2 + total
        reset_scoreboard += [f"sent_{ch}=0; got_{ch}=0; stalled_{ch}=0;"]
        chain = hierarchy in ("siblings", "fabric_direct", "fabric_routed")
        capacity = (6 if chain else 4 if hierarchy else 2) * {"buf": 1, "skid": 2, "fifo[3]": 3}[storage]
        if hierarchy == "fabric_routed" or fabric == "routed":
            capacity += 2
        capacity_checks.append(f'if(sent_{ch}!={capacity} || got_{ch}!=0) $fatal(1,"{ch} native capacity mismatch sent=%0d got=%0d",sent_{ch},got_{ch});')
        drive += [f"{ready} = epoch==0 ? 0 : (($random(seed) & 7) > 2);",
                  f"if (!{iv} || {ir}) begin",
                  f"{iv} = sent_{ch}<257 && (epoch==0 || (($random(seed) & 3) != 0));"]
        drive += [f"{incoming}_{ch}{f} = " + ((f"{bits}'h"+('0' if construction in ('fill_x','fill_z','fill_cast_x') else format((1<<bits)-1,'x'))+';') if str(construction).startswith('fill_') and f!=fields[-1][0] else "0;" if construction == "default" and f != fields[0][0]
                  else f"{bits}'({{{(bits+31)//32}{{$random(seed)}}}});") for f, bits in fields]
        if four_state:
            for index, (field, bits) in enumerate(fields):
                drive += [f"if((sent_{ch}+{index})%7==0) {incoming}_{ch}{field}[0]=1'bx;",
                          f"if((sent_{ch}+{index})%11==0) {incoming}_{ch}{field}[{bits-1}]=1'bz;"]
        if construction in ('fill_conditional','fill_cast_conditional'):
            selector=f'{incoming}_{ch}{fields[-1][0]}'
            drive += [f'{selector}=sent_{ch} & 1;']
            drive += [f"{incoming}_{ch}{f}={selector}[0] ? {bits}'h{(1<<bits)-1:x} : {bits}'h0;" for f,bits in fields[:-1]]
        drive += ["end"]
        check += [f"if(stalled_{ch} && (!{ov} || {out_packet} !== held_{ch})) $fatal(1,\"{ch} changed under stall\");",
                  f"stalled_{ch} = {ov} && !{ready}; held_{ch} = {out_packet};",
                  f"if({iv} && {ir}) begin expected_{ch}[sent_{ch}]={in_packet}; sent_{ch}=sent_{ch}+1; end",
                  f"if({ov} && {ready}) begin",
                  f"if(got_{ch}>=sent_{ch} || {out_packet} !== expected_{ch}[got_{ch}]) $fatal(1,\"{ch} field/order mismatch at %0d\",got_{ch});",
                  f"got_{ch}=got_{ch}+1; end"]
        done.append(f"got_{ch}==257")
    for name, bits, pin in observed_pins:
        declarations.append(f"logic [{bits-1}:0] {name};")
        connections.append(f".{name}({name})")
        check.append(f"if({name} !== {pin}) $fatal(1,\"named field {name} mismatch\");")
    if signed and not four_state:
        declarations.append("logic negative;")
        connections.append(".negative(negative)")
        declarations.append("logic [7:0] low_byte;")
        connections.append(".low_byte(low_byte)")
        check.append('if(negative !== ($signed(m_tdata)<0)) $fatal(1,"signed native data lost its type");')
        check.append('if(low_byte !== m_tdata[7:0]) $fatal(1,"signed native data slice mismatch");')
    tb = directory / "tb.sv"
    output_bundle = "{" + ",".join(physical_outputs) + "}"
    tb.write_text("module tb; logic clk=0,reset=1; integer seed=90210,epoch=0; always #5 clk=~clk;\n" +
                  f"logic [{physical_output_bits-1}:0] output_snapshot; logic snapshot_valid=0;\n" +
                  "\n".join(declarations) + "\nnative_relay dut(" + ",".join(connections) + ");\n" +
                  "initial begin " + "\n".join(init) +
                  "\nrepeat(4) @(negedge clk); reset=0; repeat(40) @(negedge clk);\n" + "\n".join(capacity_checks) +
                  "\nreset=1; " +
                  "repeat(3) @(negedge clk); epoch=1; reset=0; end\n" +
                  "always @(negedge clk) if(!reset) begin\n" + "\n".join(drive) + "\nend\n" +
                  f"always @(posedge clk) begin #1; snapshot_valid=!reset; output_snapshot={output_bundle}; end\n" +
                  f'always @(negedge clk) if(!reset) begin #1; if(snapshot_valid && {output_bundle} !== output_snapshot) $fatal(1,"AXI output reacted combinationally to interface inputs"); end\n' +
                  "always @(posedge clk) if(reset) begin\n" + "\n".join(reset_scoreboard) +
                  "\nend else begin\n" + "\n".join(check) +
                  "\nif(epoch==1 && " + " && ".join(done) + ") begin $display(\"PASS native isolated channel pins, reset and typed fields\"); $finish; end\nend\n" +
                  "initial begin #50000; $fatal(1,\"native relay timeout\"); end endmodule\n")
    if ordinary_package=="forward_call":
        tb.write_text(tb.read_text().replace("module tb;",'module tb; import configuration::compute; initial begin #1;if(configuration::normalize(3)!==16 || compute(3)!==16)$fatal(1,"forward package helper mismatch");end',1))
    binary = directory / "sim"
    if data_typedef=="inline_mutable" or external or qualifier:
        run(["verilator","--binary",*(["--assert"] if qualifier else []),"--timing","--top-module","tb","--Mdir",directory/"obj",
             "-o",binary,"-j","2",sv,tb,ROOT/"rtl/pigen_primitives.sv",*foreign_sources],directory)
        output=run([binary],directory)
    else:
        run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, tb,
             ROOT / "rtl/pigen_primitives.sv", *foreign_sources], directory)
        output = run(["vvp", binary], directory)
    assert "PASS native" in output
    print(f"PASS {family} data={data} storage={storage} signed={signed} binding={binding} hierarchy={hierarchy} construction={construction} alias={alias} fabric={fabric}: {257*len(channel_fields)} stalled channel records, reset and interface isolation")


def alias_negatives(directory):
    contract = "typedef bit[32] axi4_lite[addr=40] bus_t;\n"
    cases = [
        (contract + "module bad(input wire clk,input wire reset,input bus_t[addr=32] s); endmodule",
         "cannot change its field contract"),
        ("typedef bit[32] axi4_lite[clock=clk] bus_t; module bad; endmodule",
         "clock/reset belong to endpoint declarations"),
        (contract + "module bad(input wire clk,input wire reset,input bus_t port s); endmodule",
         "backpressure-preserving"),
        (contract + "module bad(input wire clk,input wire reset,input bus_t axi4 s); endmodule",
         "alias already includes its protocol"),
        (contract + "module bad(output wire [31:0] n); assign n=$bits(bus_t); endmodule",
         "native AXI endpoint type is not a packed data type"),
        (contract + "module bad(output wire [31:0] n); assign n=bus_t'(32'd1); endmodule",
         "native AXI endpoint type is not a packed data type"),
        (contract + "module bad(output wire[31:0] n); typedef logic[31:0] word_t; "
         "wire word_t other=32'(bus_t); assign n=$bits(bus_t); endmodule",
         "native AXI endpoint type is not a packed data type"),
        (contract + "module bad(output wire[31:0] n); integer other=bus_t; "
         "assign n=$bits(bus_t); endmodule",
         "native AXI endpoint type is not a packed data type"),
        (contract + "module bad(input wire clk,input wire reset,input bus_t s,"
         "output bit[32] axi4_lite[addr=32] m); fabric begin s -> m; endfabric endmodule",
         "compatible native"),
    ]
    for index, (text, diagnostic) in enumerate(cases):
        source = directory / f"alias_bad_{index}.pigen"
        output = directory / f"alias_bad_{index}.sv"
        source.write_text(text)
        output.write_text("preserve output\n")
        result = run([ROOT / "pigen", source, "-o", output], directory, expect=False)
        assert diagnostic in result, result
        assert output.read_text() == "preserve output\n"


def alias_shadow_tests(directory):
    for name, declaration, expression, width, expected in [
            ("parameter", "localparam int bus_t=7;", "$bits(bus_t)", 32, 32),
            ("value", "wire [31:0] bus_t=32'd7;", "$bits(bus_t)", 32, 32),
            ("grouped_value", "wire [31:0] first=32'd0,bus_t=32'd7;", "$bits(bus_t)", 32, 32),
            ("typed_net", "typedef logic[31:0] word_t; wire word_t bus_t=32'd7;", "$bits(bus_t)", 32, 32),
            ("grouped_typed_net", "typedef bit[15:0] word_t; wire word_t first=16'd0,bus_t=16'd7;", "$bits(bus_t)", 32, 16),
            ("byte_net", "wire byte bus_t=7;", "$bits(bus_t)", 32, 8),
            ("integer_value", "integer bus_t=7;", "$bits(bus_t)", 32, 32),
            ("grouped_integer", "integer first=0,bus_t=7;", "$bits(bus_t)", 32, 32),
            ("signed_net", "wire signed bus_t=1'b1;", "$bits(bus_t)", 32, 1),
            ("implicit_net", "wire bus_t=1'b1;", "$bits(bus_t)", 32, 1),
            ("typed_initializers", "typedef logic[31:0] word_t; wire word_t first={16'd0,16'd1},bus_t=(32'd7);", "$bits(bus_t)", 32, 32),
            ("packed_type", "typedef logic[7:0] bus_t;", "bus_t'(8'h5a)", 8, 90)]:
        source = directory / f"alias_shadow_{name}.pigen"
        sv = directory / f"alias_shadow_{name}.sv"
        tb = directory / f"alias_shadow_{name}_tb.sv"
        binary = directory / f"alias_shadow_{name}_sim"
        source.write_text("typedef bit[32] axi4_lite[addr=40] bus_t;\n"
                          f"module shadow(output wire[{width-1}:0] n); {declaration} assign n={expression}; endmodule\n")
        run([ROOT / "pigen", source, "-o", sv], directory)
        run(["verilator", "--lint-only", "--top-module", "shadow", sv], directory)
        tb.write_text(f"module tb; wire [{width-1}:0] n; shadow dut(.n(n)); initial begin #1; "
                      f"if(n !== {width}'d{expected}) $fatal(1,\"alias shadow mismatch\"); "
                      "$display(\"PASS alias shadow\"); $finish; end endmodule\n")
        run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, tb], directory)
        assert "PASS alias shadow" in run(["vvp", binary], directory)


def forward_alias_tests(directory):
    for family, options in [("axi4_stream", "last=1"), ("axi4_lite", "addr=40"), ("axi4", "addr=40,id=4")]:
        alias = f"typedef bit[32] {family}[{options}] bus_t;"
        cases = [
            ("global_port", "module bad(input wire clk,input bus_t s); endmodule " + alias, "typedef must be declared before use"),
            ("local_port", "module bad(input wire clk,input bus_t s); " + alias + " endmodule", "typedef must be declared before use"),
            ("chain", "typedef future_t bus_t; typedef bit[32] " + family + "[" + options + "] future_t; module bad; endmodule", "typedef must be declared before use"),
            ("local", "module bad(input wire clk,input wire reset); bus_t middle; " + alias + " endmodule", "typedef must be declared before use"),
            ("typed_net", "module bad; wire bus_t middle; " + alias + " endmodule", "unknown type name"),
            ("local_array", "module bad(input wire clk,input wire reset); bus_t middle[2]; " + alias + " endmodule", "typedef must be declared before use"),
            ("query", "module bad(output wire[31:0] n); assign n=$bits(bus_t); " + alias + " endmodule", "typedef must be declared before use"),
            ("cast", "module bad(output wire[31:0] n); assign n=bus_t'(32'd1); " + alias + " endmodule", "typedef must be declared before use"),
            ("global_query", "module bad(output wire[31:0] n); assign n=$bits(bus_t); endmodule " + alias, "typedef must be declared before use"),
            ("generated", "module bad(input wire clk,input wire reset); if(1) begin: g bus_t middle; " + alias + " end endmodule", "typedef must be declared before use"),
        ]
        for name, text, diagnostic in cases:
            source = directory / f"forward_alias_{family}_{name}.pigen"
            output = source.with_suffix(".sv")
            source.write_text(text)
            output.write_text("preserve output\n")
            result = run([ROOT/"pigen",source,"-o",output,"--no-diagram"],directory,expect=False)
            assert diagnostic in result, result
            assert output.read_text() == "preserve output\n"
    # Local values and retained function scopes hide a later compilation-unit type.
    for name, declaration, expression in [
            ("wire", "wire[31:0] bus_t=7;", "$bits(bus_t)"),
            ("parameter", "localparam bus_t=7;", "$bits(bus_t)"),
            ("function", "function automatic integer width_fn; integer bus_t; begin width_fn=$bits(bus_t); end endfunction", "width_fn()")]:
        source = directory/f"later_alias_shadow_{name}.pigen"
        sv, tb, sim = [source.with_suffix(suffix) for suffix in (".sv",".tb.sv",".sim")]
        source.write_text(f"module shadow(output wire[31:0] n); {declaration} assign n={expression}; endmodule\n"
                          "typedef bit[32] axi4_lite[addr=40] bus_t;\n")
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        run(["verilator","--lint-only","--top-module","shadow",sv],directory)
        tb.write_text("module tb; wire[31:0] n; shadow dut(.n(n)); initial begin #1; "
                      "if(n!==32) $fatal(1,\"later alias shadow\"); $display(\"PASS later alias shadow\"); $finish; end endmodule\n")
        run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
        assert "PASS later alias shadow" in run(["vvp",sim],directory)
    source = directory/"later_alias_nonansi.pigen"
    sv, tb, sim = [source.with_suffix(suffix) for suffix in (".sv",".tb.sv",".sim")]
    source.write_text("module shadow(bus_t,n); input wire[31:0] bus_t; output wire[31:0] n; assign n=$bits(bus_t); endmodule\n"
                      "typedef bit[32] axi4_lite[addr=40] bus_t;\n")
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    run(["verilator","--lint-only","--top-module","shadow",sv],directory)
    tb.write_text("module tb; wire[31:0] n; shadow dut(.bus_t(32'd0),.n(n)); initial begin #1; "
                  "if(n!==32) $fatal(1,\"nonansi later alias\"); $display(\"PASS later alias shadow\"); $finish; end endmodule\n")
    run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
    assert "PASS later alias shadow" in run(["vvp",sim],directory)
    print("PASS forward native alias diagnostics, output preservation and later-type lexical shadows")


def package_type_tests(directory):
    source, sv, tb, sim = [directory/name for name in ("package_types.pigen","package_types.sv","package_types_tb.sv","package_types_sim")]
    source.write_text("package bus_defs; localparam int ADDRESS_BITS=40; "
        "typedef struct packed {bit[16] hi;bit[16] lo;} word_t; "
        "typedef word_t axi4_lite[addr=ADDRESS_BITS] bus_t; endpackage\n"
        "package copies; typedef bus_defs::bus_t copied_t; endpackage\n"
        "package ordinary; localparam N=7; function integer plus_one(input integer x); plus_one=x+1; endfunction endpackage\n"
        "module package_link(input wire clk,input wire reset,input copies::copied_t s,output bus_defs::bus_t m); "
        "fabric begin s > m; endfabric endmodule\n"
        "module package_values(output wire[31:0] n); localparam CHECK=$bits(bus_defs::word_t); "
        "assign n=bus_defs::ADDRESS_BITS+CHECK+ordinary::plus_one(ordinary::N); endmodule\n")
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    for top in ("package_link","package_values"):
        run(["verilator","--lint-only","--top-module",top,sv,ROOT/"rtl/pigen_primitives.sv"],directory)
    tb.write_text("module tb; wire[31:0] n; package_values dut(.n(n)); initial begin #1; "
                  "if(n!==80) $fatal(1,\"package values\"); $display(\"PASS package values\"); $finish; end endmodule\n")
    run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
    assert "PASS package values" in run(["vvp",sim],directory)
    for family, options in [("axi4_stream","last=1"),("axi4_lite","addr=40"),("axi4","addr=40,id=4")]:
        package = f"package buses; typedef bit[32] {family}[{options}] bus_t; endpackage\n"
        cases = [
            ("forward", "module bad(input buses::bus_t s); endmodule\n"+package, "typedef must be declared before use"),
            ("missing", package+"module bad(input buses::missing_t s); endmodule", "unknown type name"),
            ("query", package+"module bad(output wire[31:0] n); assign n=$bits(buses::bus_t); endmodule", "not a packed data type"),
            ("cast", package+"module bad(output wire[31:0] n); assign n=buses::bus_t'(32'd0); endmodule", "not a packed data type"),
            ("override", package+"module bad(input wire clk,input wire reset,input buses::bus_t[id=2] s); endmodule", "cannot change its field contract"),
            ("duplicate", package+package+"module bad; endmodule", "duplicate or invalid package name"),
            ("leak", package+"module bad(input wire clk,input wire reset,input buses::bus_t s,output bit[32] axi4_stream m); "
             "bit[32] buf plain; fabric begin plain > m; endfabric endmodule", "native"),
        ]
        for name, text, diagnostic in cases:
            bad = directory/f"package_bad_{family}_{name}.pigen";output=bad.with_suffix(".sv")
            bad.write_text(text);output.write_text("preserve output\n")
            result=run([ROOT/"pigen",bad,"-o",output,"--no-diagram"],directory,expect=False)
            assert diagnostic in result,result
            assert output.read_text()=="preserve output\n"
    example = directory/"native_axi_packages.sv"
    run([ROOT/"pigen",ROOT/"examples/native_axi_packages.pigen","-o",example,"--no-diagram"],directory)
    for top in ("native_axis_package","native_axil_package","native_axi4_package"):
        run(["verilator","--lint-only","--top-module",top,example,ROOT/"rtl/pigen_primitives.sv"],directory)
    print("PASS native package namespaces, constants, records, ordinary preservation and diagnostics")


def package_import_tests(directory):
    packages = ("package first_pkg; localparam int X=5; typedef bit[8] word_t; "
                "typedef bit[32] axi4_lite[addr=40] bus_t; endpackage\n"
                "package second_pkg; localparam int X=9; typedef bit[16] word_t; "
                "typedef bit[32] axi4_lite[addr=41] bus_t; endpackage\n")
    positives = [
        ("local_shadow", "module values(output wire[31:0] n); import first_pkg::*,second_pkg::*; "
         "localparam X=11; typedef bit[23:0] word_t; localparam W=$bits(word_t); assign n=X+W; endmodule", 35),
        ("explicit", "module values import first_pkg::word_t,first_pkg::X; (output wire[31:0] n); "
         "import second_pkg::*; import first_pkg::word_t; localparam W=$bits(word_t); assign n=X+W; endmodule",13),
        ("activated", "module values(output wire[31:0] n); import first_pkg::*; localparam BEFORE=$bits(word_t); "
         "import second_pkg::*; localparam AFTER=$bits(word_t); assign n=BEFORE+AFTER; endmodule",16),
        ("deferred_body", "module values(output wire[31:0] n); import first_pkg::*; assign n=uint[32]'(X)+Z; "
         "import second_pkg::*; localparam Z=X; endmodule",10),
        ("procedural_first", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb n=uint[32]'(X)+Z; import second_pkg::*; localparam Z=X; endmodule",10),
        ("procedural_shadow", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb begin automatic uint[32] X=11; n=X+Z; end "
         "import second_pkg::X; localparam Z=X; endmodule",20),
        ("procedural_sibling", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb begin begin automatic uint[32] X=11; n=X; end n=n+uint[32]'(X)+Z; end "
         "import second_pkg::*; localparam Z=X; endmodule",21),
        ("procedural_loop_shadow", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb begin n=Z; for(uint[32] X=0;X<2;X++) n=n+X; end "
         "import second_pkg::X; localparam Z=X; endmodule",10),
        ("generated_procedural_first", "module values(output logic[31:0] n); if(1) begin: scope "
         "import first_pkg::*; always_comb n=uint[32]'(X)+Z; "
         "import second_pkg::*; localparam Z=X; end endmodule",10),
        ("procedural_foreach_shadow", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb begin automatic uint[32] a[2]='{3,4}; n=Z; foreach(a[X]) n=n+a[X]; end "
         "import second_pkg::X; localparam Z=X; endmodule",16),
        ("procedural_named_shadow", "module values(output logic[31:0] n); import first_pkg::*; "
         "always_comb begin begin:X static uint[32] value=11; end n=X.value+Z; end "
         "import second_pkg::X; localparam Z=X; endmodule",20),
        ("block_native_marker", "module values(output logic[31:0] n); always_comb begin "
         "import first_pkg::bus_t; n=32'd7; end endmodule",7),
        ("block_explicit", "module values(output logic[31:0] n); always_comb begin "
         "import first_pkg::X,first_pkg::word_t; localparam W=$bits(word_t); n=X+W; end endmodule",13),
        ("block_wildcard", "module values(output logic[31:0] n); import first_pkg::*; always_comb begin "
         "import second_pkg::*; n=uint[32]'(X); end import second_pkg::X; endmodule",9),
        ("block_nested", "module values(output logic[31:0] n); always_comb begin import first_pkg::*; "
         "begin import second_pkg::*; n=uint[32]'(X); end n=n+uint[32]'(X); end endmodule",14),
        ("block_shadow", "module values(output logic[31:0] n); always_comb begin import first_pkg::*,second_pkg::*; "
         "automatic uint[32] X=11; n=X; end endmodule",11),
        ("block_repeat", "module values(output logic[31:0] n); always_comb begin "
         "import first_pkg::X; import first_pkg::X; n=uint[32]'(X); end endmodule",5),
        ("block_size_cast", "module values(output logic[31:0] n); always_comb begin "
         "import first_pkg::*; n=uint[32]'(X'(32'hff)); end endmodule",31),
        ("intrinsic_candidate", "package choices;localparam valid=5;endpackage package other;localparam valid=11;endpackage "
         "module values(output wire[31:0] n);import choices::*;uint[32] wire value;uint[32] wire observed;assign value=0;assign observed=uint[32]'(valid(value)); "
         "import other::valid;localparam V=valid;assign n=uint[32]'(observed)+V;endmodule",12),
        ("future_function", "package late;function automatic logic[31:0] valid(input logic[31:0] value);return value+8;endfunction endpackage "
         "module values(output wire[31:0] n);uint[32] wire value;assign value=0;assign n=uint[32]'(valid(value));import late::valid;endmodule",1),
        ("unused", "module values(output wire[31:0] n); import first_pkg::*,second_pkg::*; "
         "assign n=first_pkg::X+second_pkg::X; endmodule",14),
        ("inner", "import first_pkg::*; module values import second_pkg::*; (output wire[31:0] n); "
         "localparam W=$bits(word_t); assign n=W; endmodule",16),
        ("repeat", "module values(output wire[31:0] n); import first_pkg::*; import first_pkg::*; "
         "localparam W=$bits(word_t); assign n=W+X; endmodule",13),
        ("size_casts", "module values import first_pkg::X; (output wire[31:0] n); "
         "assign n=uint[32]'(first_pkg::X'(32'hff))+uint[32]'(X'(32'hff)); endmodule",62),
        ("mixed", "package ordinary; localparam Z=3; endpackage "
         "module values import ordinary::*,first_pkg::*; (output wire[31:0] n); "
         "localparam W=$bits(word_t); assign n=W+X+Z; endmodule",16),
    ]
    for name, text, expected in positives:
        source=directory/f"import_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        source.write_text(packages+text)
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        run(["verilator","--lint-only","--top-module","values",sv],directory)
        tb.write_text(f"module tb; wire[31:0] n; values dut(.n(n)); initial begin #1; if(n!==32'd{expected}) "
                      "$fatal(1,\"import lookup mismatch\"); $display(\"PASS import lookup\"); $finish; end endmodule\n")
        run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
        assert "PASS import lookup" in run(["vvp",sim],directory)
    source=directory/"import_clocked_first.pigen";sv=source.with_suffix(".sv")
    tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
    source.write_text(packages+"module values(input wire clk,input wire reset,output logic[31:0] n); "
                      "import first_pkg::*; always @(posedge clk) if(!reset) n<=uint[32]'(X)+Z; "
                      "import second_pkg::*; localparam Z=X; endmodule")
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    run(["verilator","--lint-only","--top-module","values",sv],directory)
    tb.write_text("module tb; reg clk=0; reg reset=1; wire[31:0] n; values dut(.*); "
                  "initial begin #1;clk=1;#1;clk=0;reset=0;#1;clk=1;#1; "
                  "if(n!==32'd10) $fatal(1,\"clocked import mismatch\"); "
                  "$display(\"PASS clocked import\");$finish;end endmodule")
    run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
    assert "PASS clocked import" in run(["vvp",sim],directory)
    negatives = [
        ("clocked_ambiguous", "module bad(input wire clk,output logic[31:0] n); always @(posedge clk) begin "
         "import first_pkg::*,second_pkg::*; n<=uint[32]'(X); end endmodule", "compatible expression"),
        ("clocked_late", "module bad(input wire clk,output logic[31:0] n); always @(posedge clk) begin "
         "n<=0; import first_pkg::*; end endmodule", "declarations must precede statements"),
        ("clocked_leak", "module bad(input wire clk,output logic[31:0] n); always @(posedge clk) begin "
         "import first_pkg::*; n<=uint[32]'(X); end localparam N=X; endmodule", "unknown value name"),
        ("block_ambiguous", "module bad(output logic[31:0] n); always_comb begin "
         "import first_pkg::*,second_pkg::*; n=uint[32]'(X); end endmodule", "ambiguous wildcard package import"),
        ("block_conflict", "module bad(output logic[31:0] n); always_comb begin import first_pkg::X; "
         "import second_pkg::X; n=uint[32]'(X); end endmodule", "import conflicts"),
        ("block_late", "module bad(output logic[31:0] n); always_comb begin n=0; "
         "import first_pkg::*; end endmodule", "declarations must precede statements"),
        ("block_leak", "module bad(output logic[31:0] n); always_comb begin import first_pkg::*; "
         "n=uint[32]'(X); end localparam N=X; endmodule", "unknown value name"),
        ("block_ordinary", "package ordinary;localparam X=3;class c;int value;endclass endpackage "
         "module bad(output logic[31:0] n); always_comb begin import ordinary::*; "
         "n=uint[32]'(X); end endmodule", "procedural imports require a structured package"),
        ("ambiguous_type", "module bad import first_pkg::*,second_pkg::*; (input bus_t s); endmodule", "ambiguous wildcard package import"),
        ("ambiguous_constant", "module bad; import first_pkg::*,second_pkg::*; localparam N=X; endmodule", "ambiguous wildcard package import"),
        ("ambiguous_outer", "typedef bit[32] axi4_lite[addr=40] bus_t; module bad import first_pkg::*,second_pkg::*; "
         "(input bus_t s); endmodule", "ambiguous wildcard package import"),
        ("explicit_conflict", "module bad; import first_pkg::bus_t,second_pkg::bus_t; endmodule", "import conflicts"),
        ("local_conflict", "module bad; typedef bit[23:0] word_t; import first_pkg::word_t; endmodule", "import conflicts"),
        ("activated_conflict", "module bad; import first_pkg::*; typedef word_t chosen_t; typedef bit[23:0] word_t; endmodule", "duplicate typedef"),
        ("missing", "module bad; import first_pkg::missing_t; endmodule", "exported member"),
        ("no_reexport", "package relay_pkg; import first_pkg::*; endpackage "
         "module bad; import relay_pkg::bus_t; endmodule", "exported member"),
        ("iterator_not_use", "module bad(output wire[31:0] n); if(1) begin: scope import first_pkg::*; "
         "for(genvar X=0;X<1;X++) begin: lane end import second_pkg::*; localparam N=X; assign n=N; end endmodule", "ambiguous wildcard package import"),
        ("native_query", "module bad import first_pkg::bus_t; (output wire[31:0] n); assign n=$bits(bus_t); endmodule", "not a packed data type"),
        ("native_cast", "module bad import first_pkg::bus_t; (output wire[31:0] n); assign n=bus_t'(32'd0); endmodule", "not a packed data type"),
        ("before_import", "module bad(output wire[31:0] n); assign n=uint[32]'(X); import first_pkg::*; endmodule", "unknown value name"),
        ("forward", "import later_pkg::*; package later_pkg; typedef bit[32] axi4_lite bus_t; endpackage module bad; endmodule", "declared before import"),
    ]
    for name, text, diagnostic in negatives:
        source=directory/f"import_bad_{name}.pigen";output=source.with_suffix(".sv")
        source.write_text(packages+text);output.write_text("preserve output\n")
        result=run([ROOT/"pigen",source,"-o",output,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert output.read_text()=="preserve output\n"
    print(f"PASS package imports: {len(positives)+1} independent value simulations, lazy collisions/activation, scope order and {len(negatives)} preserved-output diagnostics")


def ordinary_package_tests(directory):
    definition="package cfg;localparam int W=8;typedef bit[7:0] word_t;endpackage "
    positives=[
        ("qualified",definition+"module values(output wire[31:0] n);assign n=uint[32]'(cfg::W)+$bits(cfg::word_t);endmodule",16),
        ("header",definition+"module values import cfg::*; (output wire[31:0] n);assign n=uint[32]'(W)+$bits(word_t);endmodule",16),
        ("block",definition+"module values(output logic[31:0] n);always_comb begin import cfg::*;n=uint[32]'(W)+$bits(word_t);end endmodule",16),
        ("transitive",definition+"package bridge;import cfg::*;typedef word_t copied_t;localparam N=W+1;export *::*;endpackage "
         "module values(output wire[31:0] n);assign n=uint[32]'(bridge::N)+$bits(bridge::copied_t);endmodule",17),
        ("ordinary_ports","package cfg;localparam W=32;typedef bit[31:0] word_t;endpackage "
         "module typed_marker(output wire[31:0] n);assign n=uint[32]'(cfg::W);endmodule "
         "module values(output cfg::word_t n);assign n=cfg::word_t'(32'd5);endmodule",5),
        ("unrelated_package",definition+"package untouched;function automatic int bump(input int x);return x+1;endfunction endpackage "
         "package spare;localparam int fifo=untouched::bump(1);endpackage "
         "module typed_marker(output wire[31:0] n);assign n=uint[32]'(cfg::W);endmodule "
         "module values(output wire[31:0] n);assign n=untouched::bump(32'd7)+cfg::W;endmodule",16),
        ("preserved_process",definition+"module typed_marker(output wire[31:0] n);assign n=uint[32]'(cfg::W);endmodule "
         "module values(output logic[31:0] n);function automatic logic[31:0] transform(input logic[31:0] v);return v+1;endfunction "
         "always_comb begin import cfg::*;n=transform(32'd7)+32'(W);end endmodule",16),
    ]
    for name,text,expected in positives:
        source=directory/f"ordinary_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        source.write_text(text)
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        run(["verilator","--lint-only","--top-module","values",sv],directory)
        if name=="unrelated_package":
            assert "package untouched;function automatic int bump(input int x);return x+1;endfunction endpackage" in sv.read_text()
            assert "package spare;localparam int fifo=untouched::bump(1);endpackage" in sv.read_text()
        if name=="preserved_process":
            assert "function automatic logic[31:0] transform(input logic[31:0] v);return v+1;endfunction" in sv.read_text()
            assert "always_comb begin import cfg::*;n=transform(32'd7)+32'(W);end" in sv.read_text()
        tb.write_text(f'module tb;wire[31:0] n;values dut(.*);initial begin #1;if(n!==32\'d{expected}) '
                      '$fatal(1,"ordinary package mismatch");$display("PASS ordinary packages");$finish;end endmodule')
        run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
        assert "PASS ordinary packages" in run(["vvp",sim],directory)
    source=directory/"ordinary_buffered_helper.pigen";sv=source.with_suffix(".sv")
    tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
    source.write_text("package cfg;localparam int W=8;typedef bit[W-1:0] word_t;endpackage "
                      "module buffered(input wire clk,input wire reset,input cfg::word_t buf s,output cfg::word_t buf m);"
                      "always @(posedge clk) if(!reset) m<=s;endmodule")
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    run(["verilator","--lint-only","--top-module","buffered",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
    tb.write_text("module tb;reg clk=0,reset=1,s_valid=0,m_ready=0;reg[7:0] s;wire s_ready,m_valid;wire[7:0] m;"
                  "buffered dut(.*);always #5 clk=~clk;integer writes=0,reads=0,cycles=0;"
                  "initial begin repeat(2) @(negedge clk);reset=0;end "
                  "always @(negedge clk) if(!reset) begin s_valid=writes<64;s=8'(writes*7+3);m_ready=cycles%5!=2;"
                  "cycles++;if(cycles>1000)$fatal(1,\"buffered timeout\");end "
                  "always @(posedge clk) if(!reset) begin if(s_valid&&s_ready)writes++;if(m_valid&&m_ready)begin "
                  "if(m!==8'(reads*7+3))$fatal(1,\"buffered DATA mismatch\");reads++;"
                  "if(reads==64)begin $display(\"PASS ordinary buffered helper\");$finish;end end end endmodule")
    run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
    assert "PASS ordinary buffered helper" in run(["vvp",sim],directory)
    negatives=[
        ("missing_type",definition+"module bad(input cfg::missing_t axi4_stream s);endmodule","unknown"),
        ("duplicate", "package cfg;localparam W=8,W=9;typedef bit[7:0] word_t;endpackage "
         "module bad(input cfg::word_t axi4_stream s);endmodule","duplicate"),
        ("mixed_package", "package cfg;typedef bit[7:0] word_t;class c;int value;endclass endpackage "
         "module bad(input cfg::word_t axi4_stream s);endmodule","unknown"),
    ]
    for name,text,diagnostic in negatives:
        source=directory/f"ordinary_bad_{name}.pigen";sv=source.with_suffix(".sv")
        source.write_text(text);sv.write_text("preserve output\n")
        result=run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert sv.read_text()=="preserve output\n"
    print(f"PASS ordinary packages: {len(positives)} independent value simulations, transitive ownership, preserved ordinary procedures and {len(negatives)} preserved-output diagnostics")


def package_subroutine_tests(directory):
    function="function automatic logic[31:0] bump(input logic[31:0] value);localparam int W=8;return value+W;endfunction"
    task="task automatic assign_word(input logic[31:0] value,output logic[31:0] result);result=value+8;endtask : assign_word"
    legacy="function automatic logic[31:0] old_style;input logic[31:0] value;old_style=value+8;endfunction : old_style"
    narrow="function automatic logic[$bits(word_t)-1:0] narrow(input word_t value);return value^32'h5a;endfunction"
    header=("package cfg;localparam int W=32;typedef bit[W-1:0] word_t; "+function+"\n"+task+"\n"+legacy+"\n"+narrow+"\nendpackage ")
    marker=("module marker(input wire clk,input wire reset,input cfg::word_t axi4_stream s,output cfg::word_t axi4_stream m);"
            "always @(posedge clk) if(!reset) m<=s;endmodule ")
    cases=[
        ("qualified", "module values(output wire[31:0] n);assign n=cfg::bump(32'd3);endmodule",11),
        ("explicit", "module values import cfg::bump; (output wire[31:0] n);assign n=bump(32'd3);endmodule",11),
        ("task", "module values(output logic[31:0] n);always_comb cfg::assign_word(32'd3,n);endmodule",11),
        ("task_import", "module values import cfg::assign_word; (output logic[31:0] n);always_comb assign_word(32'd3,n);endmodule",11),
        ("legacy", "module values(output wire[31:0] n);assign n=cfg::old_style(32'd3);endmodule",11),
        ("legacy_import", "module values import cfg::old_style; (output wire[31:0] n);assign n=old_style(32'd3);endmodule",11),
        ("sized_return", "module values import cfg::narrow; (output wire[31:0] n);assign n=narrow(32'd3);endmodule",89),
        ("exported", "package bridge;import cfg::bump,cfg::assign_word,cfg::word_t;export *::*;endpackage "
         "module values import bridge::bump; (output wire[31:0] n);assign n=bump(32'd3);endmodule",11),
        ("exported_task", "package bridge;import cfg::assign_word,cfg::word_t;export *::*;endpackage "
         "module values import bridge::assign_word; (output logic[31:0] n);always_comb assign_word(32'd3,n);endmodule",11),
        ("early_export", "package bridge;export cfg::bump,cfg::word_t;import cfg::*;endpackage "
         "module values import bridge::bump; (output wire[31:0] n);assign n=bump(32'd3);endmodule",11),
    ]
    for name,text,expected in cases:
        source=directory/f"subroutine_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        if name in ("exported","early_export","exported_task"):
            split=text.index("module values")
            source.write_text(header+text[:split]+marker.replace("cfg::word_t","bridge::word_t")+text[split:])
        else:
            source.write_text(header+marker+text)
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        emitted=sv.read_text()
        assert function in emitted and task in emitted and legacy in emitted and narrow in emitted
        run(["verilator","--lint-only","--top-module","values",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
        tb.write_text(f'module tb;wire[31:0] n;values dut(.*);initial begin #1;if(n!==32\'d{expected}) '
                      '$fatal(1,"package subroutine mismatch");$display("PASS package subroutines");$finish;end endmodule')
        if name=="task":
            build=directory/"qualified_task_obj"
            run(["verilator","--binary","--timing","-j","2","--top-module","tb","--Mdir",build,
                 "-o",sim,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
            assert "PASS package subroutines" in run([sim],directory)
        else:
            run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
            assert "PASS package subroutines" in run(["vvp",sim],directory)
    negatives=[
        ("intrinsic_collision", "package cfg;typedef bit[31:0] word_t;function automatic logic[31:0] valid(input logic[31:0] value);return value+8;endfunction endpackage "
         "module bad import cfg::valid; (input logic[31:0] value,output wire[31:0] n);assign n=uint[32]'(valid(value));endmodule",
         "require ordinary SystemVerilog context"),
        ("typed_call",header+"module bad(output wire[31:0] n);assign n=uint[32]'(cfg::bump(3));endmodule", "require ordinary SystemVerilog context"),
        ("typed_import",header+"module bad import cfg::bump; (output wire[31:0] n);assign n=uint[32]'(bump(3));endmodule", "require ordinary SystemVerilog context"),
        ("numeric_body", "package cfg;typedef bit[31:0] word_t;function automatic uint[32] bad(input uint[32] value);return value;endfunction endpackage "+marker,
         "requires ordinary SystemVerilog constructs"),
        ("native_body", "package cfg;typedef bit[31:0] word_t;function bit[31:0] axi4_stream invalid();return 0;endfunction endpackage "+marker,
         "requires ordinary SystemVerilog constructs"),
        ("duplicate", "package cfg;typedef bit[31:0] word_t;function int bump();return 3;endfunction function int bump();return 4;endfunction endpackage "+marker,
         "duplicate or invalid package subroutine name"),
    ]
    for name,text,diagnostic in negatives:
        source=directory/f"subroutine_bad_{name}.pigen";sv=source.with_suffix(".sv")
        source.write_text(text);sv.write_text("preserve output\n")
        result=run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert sv.read_text()=="preserve output\n"
    print(f"PASS retained package subroutines: {len(cases)} independent value simulations, named imports/exports, byte-exact bodies and {len(negatives)} preserved-output diagnostics")


def retained_namespace_tests(directory):
    origin=("package origin;localparam int X=7;typedef bit[31:0] word_t;"
            "typedef struct packed {logic[31:0] X;} record_t;endpackage "
            "package other;localparam int X=13;endpackage "
            "package third;localparam int X=17;endpackage "
            "package opaque_values;class opaque_class;endclass localparam int X=23;endpackage ")
    start="package relay;import origin::*;typedef word_t payload_t;"
    finish="export *::*;endpackage "
    marker=("module marker(input wire clk,input wire reset,input relay::payload_t axi4_stream s,"
            "output relay::payload_t axi4_stream m);always @(posedge clk) if(!reset) m<=s;endmodule ")
    exported="localparam N=X;"
    shadowed="import other::X;localparam N=X;"
    scoped="import third::X;localparam N=X;"
    function=lambda body: "function automatic int value();"+body+" endfunction "
    positives=[
        ("outer_reference",function("return X;"),exported,7,"value()",7),
        ("formal_shadow","function automatic int value(input int X);return X;endfunction ",shadowed,13,"value(99)",99),
        ("old_formal_shadow","function automatic integer value;input X;integer X;value=X;endfunction ",shadowed,13,"value(99)",99),
        ("local_shadow",function("int X=99;return X;"),shadowed,13,"value()",99),
        ("constant_shadow",function("localparam X=99;return X;"),shadowed,13,"value()",99),
        ("typedef_shadow",function("typedef int X;X result=99;return result;"),shadowed,13,"value()",99),
        ("nested_scope",function("begin int X=99;X=X+1;end return X;"),exported,7,"value()",7),
        ("loop_scope",function("int sum=0;for(int X=0;X<2;X++) sum+=X;return X;"),exported,7,"value()",7),
        ("loop_shadow",function("int sum=0;for(int X=0;X<2;X++) sum+=X;return sum;"),shadowed,13,"value()",1),
        ("foreach_shadow",function("int data[2]='{4,5};int sum=0;foreach(data[X]) sum+=data[X];return sum;"),shadowed,13,"value()",9),
        ("struct_field",function("struct packed {int X;} data;data.X=99;return data.X;"),shadowed,13,"value()",99),
        ("union_field",function("union packed {int X;int Y;} data;data.X=99;return data.Y;"),shadowed,13,"value()",99),
        ("enum_shadow",function("typedef enum int {X=99} selected_t;return int'(X);"),shadowed,13,"value()",99),
        ("local_import",function("import other::X;return X;"),scoped,17,"value()",13),
        ("local_wildcard",function("import other::*;return X;"),scoped,17,"value()",13),
        ("nearer_import",function("int X=99;begin import other::*;return X;end"),scoped,17,"value()",13),
        ("qualified_reference",function("return origin::X;"),shadowed,13,"value()",7),
        ("pattern_field",function("origin::record_t data;data='{X:32'd99};return int'(data.X);"),shadowed,13,"value()",99),
        ("block_label",function("begin:X int result=99;return result;end:X"),shadowed,13,"value()",99),
        ("return_width","function automatic logic[X-1:0] value();return 7'd99;endfunction ",exported,7,"value()",99),
        ("formal_width","function automatic int value(input logic[X-1:0] argument);return int'(argument);endfunction ",exported,7,"value(7'd99)",99),
        ("case_label",function("case(7) X:return X;default:return 0;endcase"),exported,7,"value()",7),
        ("declarator_list",function("int X=98,Y=X+1;return Y;"),shadowed,13,"value()",99),
        ("formal_continuation","function automatic int value(input int a,X);return a+X;endfunction ",shadowed,13,"value(1,98)",99),
        ("conditional_scope",function("if(1) begin int X=99;return X;end else return 0;"),shadowed,13,"value()",99),
        ("opaque_explicit",function("import opaque_values::X;return X;"),shadowed,13,"value()",23),
        ("opaque_wildcard",function("import opaque_values::*;return X;"),shadowed,13,"value()",23),
    ]
    for name,body,tail,expected,call,result in positives:
        source=directory/f"retained_namespace_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        packages=origin+start+body+tail+finish
        source.write_text(packages+marker+"module values(output wire[31:0] n);assign n=uint[32]'(relay::N);endmodule")
        oracle=source.with_suffix(".oracle.sv")
        oracle.write_text(packages+"module values(output wire[31:0] n);assign n=relay::N;endmodule")
        run(["verilator","--lint-only","--top-module","values",oracle],directory)
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        assert body in sv.read_text()
        run(["verilator","--lint-only","--top-module","values",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
        tb.write_text(f"module tb;wire[31:0] n;values dut(.*);initial begin #1;if(n!==32'd{expected} || int'(relay::{call})!=={result}) "
                      '$fatal(1,"retained namespace mismatch");$display("PASS retained namespace");$finish;end endmodule')
        # Compare original and emitted SV with Icarus where its retained-body
        # support permits it. Verilator 5.020 misbinds nearer_import; Icarus
        # agrees with IEEE 1800-2017 26.3 for that case.
        if name not in {"loop_shadow","foreach_shadow","struct_field","union_field","enum_shadow","pattern_field","declarator_list","opaque_explicit","opaque_wildcard"}:
            for text in (oracle,sv):
                run(["iverilog","-g2012","-s","tb","-o",sim,text,tb],directory)
                assert "PASS retained namespace" in run(["vvp",sim],directory)
            continue
        # Icarus lacks several otherwise legal retained function-body forms.
        build=directory/f"retained_namespace_{name}_obj"
        run(["verilator","--binary","--timing","-j","2","--top-module","tb","--Mdir",build,"-o",sim,
             sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
        assert "PASS retained namespace" in run([sim],directory)
    negatives=[
        ("ambiguous",start.replace("import origin::*;","import origin::*,other::*;")+function("return X;")+exported+finish,"ambiguous wildcard package import in retained subroutine"),
        ("activated_conflict",start+function("return X;")+shadowed+finish,"package import conflicts"),
        ("local_not_exported",start+function("int X=99;return X;")+finish,"unknown value name"),
        ("formal_not_exported",start+"function int value(input int X);return X;endfunction "+finish,"unknown value name"),
        ("import_not_exported",start+function("import other::*;return X;")+finish,"unknown value name"),
        ("field_not_exported",start+function("struct packed {int X;} data;data.X=99;return data.X;")+finish,"unknown value name"),
        ("for_not_exported",start+function("int sum=0;for(int X=0;X<2;X++)sum+=X;return sum;")+finish,"unknown value name"),
        ("opaque_not_exported",start+function("import opaque_values::*;return X;")+finish,"unknown value name"),
        ("local_import_conflict",start+function("int X=0;import other::X;return X;")+exported+finish,"retained subroutine import requires"),
        ("import_local_conflict",start+function("import other::X;int X=0;return X;")+exported+finish,"retained declaration conflicts"),
    ]
    for name,relay,diagnostic in negatives:
        source=directory/f"retained_namespace_bad_{name}.pigen";sv=source.with_suffix(".sv")
        source.write_text(origin+relay+marker+"module values(output wire[31:0] n);assign n=uint[32]'(relay::X);endmodule")
        sv.write_text("preserve output\n")
        result=run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert sv.read_text()=="preserve output\n"
    print(f"PASS retained namespaces: {len(positives)} independent simulations and ordinary-SV oracles, {len(negatives)} preserved-output diagnostics")


def forward_package_call_tests(directory):
    libraries=("package first_calls;localparam int X=5;typedef bit[31:0] word_t;"
               "function automatic int g(input int value=3);return value+7;endfunction endpackage "
               "package later_calls;localparam int X=9;function automatic int g(input int value=3);return value+13;endfunction endpackage "
               "package third_calls;localparam int X=17;function automatic int g(input int value=3);return value+21;endfunction endpackage ")
    f=lambda expression: "function automatic int evaluate(input int value);return "+expression+";endfunction "
    initial="import first_calls::*;typedef word_t payload_t;"
    marker=("module marker(input wire clk,input wire reset,input relay::payload_t axi4_stream s,"
            "output relay::payload_t axi4_stream m);always @(posedge clk) if(!reset)m<=s;endmodule ")
    cases=[
        ("future_explicit",initial,f("g(value)"),"import later_calls::g;", "later_calls", "later_calls::g(v)+later_calls::g(v)"),
        ("qualified_export",initial,f("g(value)"),"import later_calls::g;", "later_calls", "later_calls::g(v)+later_calls::g(v)"),
        ("future_declaration",initial,f("g(value)"),"function automatic int g(input int value=3);return value+21;endfunction ",
         "relay", "third_calls::g(v)+third_calls::g(v)"),
        ("ambiguous_explicit",initial.replace("import first_calls::*;","import first_calls::*,later_calls::*;"),f("g(value)"),
         "import third_calls::g;", "third_calls", "third_calls::g(v)+third_calls::g(v)"),
        ("ambiguous_declaration",initial.replace("import first_calls::*;","import first_calls::*,later_calls::*;"),f("g(value)"),
         "function automatic int g(input int value=3);return value+21;endfunction ", "relay", "third_calls::g(v)+third_calls::g(v)"),
        ("wildcard_freeze",initial,f("g(value)"),"import later_calls::*;function automatic int late_evaluate(input int value);return g(value);endfunction ",
         "first_calls", "first_calls::g(v)+first_calls::g(v)"),
        ("local_import",initial,"function automatic int evaluate(input int value);int cached=g(value);import later_calls::g;return cached;endfunction ",
         "import third_calls::g;", "later_calls", "later_calls::g(v)+third_calls::g(v)"),
        ("nested_import",initial,"function automatic int evaluate(input int value);begin int cached=g(value);import later_calls::g;return cached;end endfunction ",
         "import third_calls::g;", "later_calls", "later_calls::g(v)+third_calls::g(v)"),
        ("late_local_wildcard",initial,"function automatic int evaluate(input int value);int cached=g(value);import later_calls::*;return cached;endfunction ",
         "", "first_calls", "first_calls::g(v)+first_calls::g(v)"),
        ("value_order",initial,f("X+g(value)"),"import later_calls::*;import third_calls::g;localparam N=X;", "third_calls",
         "first_calls::X+third_calls::g(v)+third_calls::g(v)"),
        ("nested_calls",initial,f("g(g(value))"),"import third_calls::g;", "third_calls", "third_calls::g(third_calls::g(v))+third_calls::g(v)"),
        ("named_arguments",initial,f("g(.value(value))"),"import later_calls::g;", "later_calls", "later_calls::g(v)+later_calls::g(v)"),
        ("object_macro",initial,f("`SELECT(value)"),"import later_calls::g;", "later_calls", "later_calls::g(v)+later_calls::g(v)"),
        ("function_macro",initial,f("`APPLY(g,value)"),"import later_calls::g;", "later_calls", "later_calls::g(v)+later_calls::g(v)"),
    ]
    prefix="`define SELECT g\n`define APPLY(name,argument) name(argument)\n"
    for name,imports,body,tail,chosen,reference in cases:
        source=directory/f"forward_call_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        packages=libraries+"package relay;"+imports+body+tail+"export *::*;endpackage "
        public_call="relay::g(v)" if name=="qualified_export" else "g(v)"
        source.write_text(prefix+packages+marker+"module values import relay::g; (input wire[31:0] v,output wire[31:0] n);assign n=relay::evaluate(v)+"+public_call+";endmodule")
        original=source.with_suffix(".original.sv")
        original.write_text(prefix+packages+"module values import relay::g; (input wire[31:0] v,output wire[31:0] n);assign n=relay::evaluate(v)+g(v);endmodule")
        run(["verilator","--lint-only","--top-module","values",original],directory)
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        emitted=sv.read_text()
        assert chosen+"::g" in emitted,emitted
        run(["verilator","--lint-only","--top-module","values",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
        tb.write_text("module tb;reg[31:0] v;wire[31:0] n;wire[31:0] expected="+reference+";values dut(.*);"
                      "initial begin for(integer i=0;i<64;i++)begin v=i;#1;if(n!==expected)$fatal(1,\"forward call mismatch\");end "
                      '$display("PASS forward calls");$finish;end endmodule')
        if name in {"named_arguments","future_declaration","ambiguous_declaration","qualified_export"}:
            build=directory/f"forward_call_{name}_obj"
            run(["verilator","--binary","--timing","-j","2","--top-module","tb","--Mdir",build,"-o",sim,sv,tb],directory)
            assert "PASS forward calls" in run([sim],directory)
        else:
            run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
            assert "PASS forward calls" in run(["vvp",sim],directory)
    # Parent wildcard candidates introduced after a call cannot capture it.
    source=directory/"forward_call_future_wildcard.pigen";sv=source.with_suffix(".sv")
    source.write_text(libraries+"import first_calls::g;package relay;import first_calls::word_t;typedef word_t payload_t;"+
                      f("g(value)")+"import later_calls::*;export *::*;endpackage "+marker+
                      "module values(input wire[31:0] v,output wire[31:0] n);assign n=relay::evaluate(v);endmodule")
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    assert "return first_calls::g(value)" in sv.read_text()
    run(["verilator","--lint-only","--top-module","values",sv],directory)
    tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
    tb.write_text('module tb;reg[31:0] v=3;wire[31:0] n;values dut(.*);initial begin #1;if(n!==10)$fatal(1,"late wildcard capture");$display("PASS late wildcard");$finish;end endmodule')
    run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory);assert "PASS late wildcard" in run(["vvp",sim],directory)
    negatives=[
        ("ambiguous",initial.replace("import first_calls::*;","import first_calls::*,later_calls::*;")+f("g(value)"),"ambiguous wildcard package import in retained subroutine call"),
        ("value_before_import",initial+f("X+g(value)")+"import later_calls::X;","package import conflicts"),
    ]
    for name,body,diagnostic in negatives:
        source=directory/f"forward_call_bad_{name}.pigen";sv=source.with_suffix(".sv")
        source.write_text(libraries+"package relay;"+body+"export *::*;endpackage "+marker)
        sv.write_text("preserve output\n");result=run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert sv.read_text()=="preserve output\n"
    tasks=("package first_tasks;typedef bit[31:0] word_t;task automatic g(input int value,output int result);result=value+7;endtask endpackage "
           "package later_tasks;task automatic g(input int value,output int result);result=value+13;endtask endpackage ")
    for named in (False,True):
        source=directory/f"forward_task_{named}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        arguments=".value(value),.result(result)" if named else "value,result"
        source.write_text(tasks+"package relay;import first_tasks::*;typedef word_t payload_t;"
                          "task automatic evaluate(input int value,output int result);g("+arguments+");endtask "
                          "import later_tasks::g;export *::*;endpackage "+marker+
                          "module values import relay::g; (input wire[31:0] v,output logic[31:0] n);logic[31:0] a,b;"
                          "always_comb begin relay::evaluate(v,a);g(v,b);n=a+b;end endmodule")
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        assert "later_tasks::g(" in sv.read_text()
        run(["verilator","--lint-only","--top-module","values",sv],directory)
        tb.write_text('module tb;reg[31:0] v;wire[31:0] n;values dut(.*);initial begin for(integer i=0;i<64;i++)begin '
                      'v=i;#1;if(n!==2*(v+13))$fatal(1,"forward task mismatch");end $display("PASS forward tasks");$finish;end endmodule')
        run(["verilator","--binary","--timing","-j","2","--top-module","tb","--Mdir",directory/f"forward_task_{named}_obj",
             "-o",sim,sv,tb],directory)
        assert "PASS forward tasks" in run([sim],directory)
    source=directory/"forward_task_no_arguments.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
    source.write_text('package first_tasks;typedef bit[31:0] word_t;task notify;$display("FIRST task");endtask endpackage '
                      'package later_tasks;task notify;$display("LATER task");endtask endpackage '
                      'package relay;import first_tasks::*;typedef word_t payload_t;task evaluate;notify;endtask '
                      'import later_tasks::notify;endpackage '+marker)
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    assert "later_tasks::notify();" in sv.read_text()
    tb.write_text('module tb;initial begin relay::evaluate();$display("PASS forward bare task");$finish;end endmodule')
    run(["verilator","--binary","--timing","-j","2","--top-module","tb","--Mdir",directory/"forward_bare_task_obj","-o",sim,sv,tb],directory)
    output=run([sim],directory)
    assert "LATER task" in output and "FIRST task" not in output and "PASS forward bare task" in output,output
    print(f"PASS forward package calls: {len(cases)*64+1} independently referenced function values, 128 task values, bare tasks, macros and preserved-output diagnostics")


def package_export_tests(directory):
    origin = ("package origin; localparam X=5; typedef bit[8] word_t; "
              "typedef bit[32] axi4_lite[addr=40] bus_t; endpackage\n")
    positives = [
        ("named", "package relay; import origin::*; export origin::X,origin::word_t; endpackage",13),
        ("activated", "package relay; import origin::*; localparam N=$bits(word_t)+X; export origin::*; endpackage",13),
        ("before_import", "package relay; export origin::X,origin::word_t; import origin::*; endpackage",13),
        ("early_reference", "package relay;export origin::X,origin::word_t;localparam N=X; "
         "typedef word_t local_t;import origin::*;endpackage",13),
        ("all", "package relay; export *::*; import origin::X,origin::word_t; endpackage",13),
        ("diamond", "package left_pkg;import origin::*;export origin::X,origin::word_t;endpackage "
         "package relay;import left_pkg::*,origin::*;export *::*;localparam N=$bits(word_t)+X;endpackage",13),
        ("binding_order", "package other;localparam X=9;typedef bit[32] axi4_lite bus_t;endpackage "
         "package relay;import origin::*;export origin::X,origin::word_t;import other::*;localparam N=X;endpackage",13),
        ("shadow", "package relay;import origin::*;typedef bit[16] word_t;localparam X=7;export origin::*;endpackage",23),
        ("repeat", "package relay;import origin::*;export origin::X,origin::word_t; "
         "export origin::X;import origin::X;endpackage",13),
    ]
    for name, relay, expected in positives:
        source=directory/f"export_{name}.pigen";sv=source.with_suffix(".sv");tb=source.with_suffix(".tb.sv");sim=source.with_suffix(".sim")
        source.write_text(origin+relay+" module values import relay::*; (output wire[31:0] n); "
                          "assign n=uint[32]'(X)+$bits(word_t); endmodule")
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        run(["verilator","--lint-only","--top-module","values",sv],directory)
        tb.write_text(f'module tb;wire[31:0] n;values dut(.*);initial begin #1;if(n!==32\'d{expected}) '
                      '$fatal(1,"export mismatch");$display("PASS exports");$finish;end endmodule')
        run(["iverilog","-g2012","-s","tb","-o",sim,sv,tb],directory)
        assert "PASS exports" in run(["vvp",sim],directory)
    negatives = [
        ("unused", "package relay;import origin::*;export origin::*;endpackage "
         "module bad(input relay::bus_t s);endmodule", "unknown"),
        ("missing_route", "package relay;export origin::bus_t;endpackage module bad;endmodule", "matching imported"),
        ("missing_member", "package relay;import origin::*;export origin::missing;endpackage module bad;endmodule", "matching imported"),
        ("local_conflict", "package relay;import origin::*;export origin::X;localparam X=9;endpackage module bad;endmodule", "duplicate"),
        ("all_name", "package relay;import origin::*;export *::X;endpackage module bad;endmodule", "package import requires"),
        ("no_implicit", "package relay;import origin::*;endpackage module bad(input relay::bus_t s);endmodule", "unknown"),
    ]
    for name, text, diagnostic in negatives:
        source=directory/f"export_bad_{name}.pigen";sv=source.with_suffix(".sv")
        source.write_text(origin+text);sv.write_text("preserve output\n")
        result=run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory,expect=False)
        assert diagnostic in result,result
        assert sv.read_text()=="preserve output\n"
    print(f"PASS package exports: {len(positives)} independent value simulations and {len(negatives)} preserved-output diagnostics")


def named_type_example_tests(directory):
    example = ROOT / "examples/native_axi_named_types.pigen"
    sv = directory / "named_types.sv"
    run([ROOT / "pigen", example, "-o", sv, "--no-diagram"], directory)
    for top in ("native_axis_named_types", "native_axil_named_types", "native_axi4_named_types",
                "native_axis_boundary_child", "native_axil_boundary_child", "native_axi4_boundary_child"):
        run(["verilator", "--lint-only", "--top-module", top, sv,
             ROOT / "rtl/pigen_primitives.sv"], directory)
    for family, core, data in [("axi4_lite", "native_axil_named_types", 32),
                               ("axi4", "native_axi4_named_types", 64),
                               ("axi4_lite", "native_axil_boundary_child", 32),
                               ("axi4", "native_axi4_boundary_child", 64)]:
        for address, identity in [(24, 2), (48, 6)]:
            protocol = f"bit[{data}] {family}[addr={address}" + (f",id={identity}" if family == "axi4" else "") + "]"
            overrides = f".ADDRESS_BITS({address})" + (f",.ID_BITS({identity})" if family == "axi4" else "")
            source = directory / f"named_types_{core}_{address}.pigen"
            output = source.with_suffix(".sv")
            source.write_text(example.read_text() +
                              f"\nmodule override_top(input wire clk,input wire reset,input {protocol} s,output {protocol} m);\n"
                              f"{core} #({overrides}) child(.clk(clk),.reset(reset),.s(s),.m(m));\nendmodule\n")
            run([ROOT / "pigen", source, "-o", output, "--no-diagram"], directory)
            run(["verilator", "--lint-only", "--top-module", "override_top", output,
                 ROOT / "rtl/pigen_primitives.sv"], directory)
    print("PASS named AXI type examples and module-local 24/48-bit address overrides")


def negatives(directory):
    bad = [
        ("uint[24] axi4_lite x", "invalid AXI protocol configuration"),
        ("uint[32] axi4_lite[id=0] x", "not applicable"),
        ("uint[32] axi4_stream[addr=0] x", "not applicable"),
        ("uint[32] axi4[user=0] x", "not applicable"),
        ("uint[32] axi4_stream[keep=2] x", "zero or one"),
        ("uint[32] axi4[addr=-1] x", "nonnegative concrete"),
        ("uint[32] axi4[id=4,id=4] x", "duplicate AXI"),
        ("uint[32] axi4[unknown=1] x", "unknown AXI"),
        ("uint[32] axi4[addr=] x", "expression"),
        ("uint[32] axi4[addr=32,] x", "comma"),
        ("uint[32] axi4 port x", "backpressure-preserving"),
        ("uint[32] axi4 wire x", "backpressure-preserving"),
    ]
    for index, (declaration, diagnostic) in enumerate(bad):
        source = directory / f"bad{index}.pigen"
        source.write_text(f"module bad(input wire clk,input wire reset,input {declaration}); endmodule\n")
        output = run([ROOT / "pigen", source, "-o", directory / "bad.sv"], directory, expect=False)
        assert diagnostic in output, f"negative {index}: {output}"
    incompatible = [
        ("input uint[32] axi4_stream[id=4] s, output uint[32] axi4_stream[dest=4] m",
         "m <= s;", "compatible protocol contracts"),
        ("input uint[32] axi4_stream[id=4] s, output uint[32] axi4_stream[dest=4] m",
         "when (m <= s) begin end", "compatible protocol contracts"),
        ("input uint[32] buf s, output uint[32] axi4_stream m",
         "m <= s;", "typed channel or explicit record"),
        ("input uint[32] axi4[addr=6] s, output uint[32] axi4_lite[addr=32] m",
         "m.aw <= s.aw;", "compatible protocol contracts"),
        ("input uint[32] axi4 s, output uint[32] axi4 m",
         "m.aw <= s.aw.addr;", "complete record"),
    ]
    incompatible += [
        ('input uint[32] axi4_stream[id=4] s, output uint[32] axi4_stream[dest=4] m',
         'm <= s.data[0] ? s : s;', 'compatible protocol contracts'),
        ('input uint[32] axi4_stream[id=4] s, output uint[32] axi4_stream[dest=4] m',
         "m <= s.data[0] ? '{data:s.data,dest:s.id} : s;", 'compatible protocol contracts'),
    ]
    for index, (ports, body, diagnostic) in enumerate(incompatible):
        source = directory / f"copy_bad{index}.pigen"
        source.write_text(f"module bad(input wire clk,input wire reset,{ports});\n"
                          f"always @(posedge clk) if(!reset) begin {body} end\nendmodule\n")
        output = run([ROOT / "pigen", source, "-o", directory / "bad.sv"], directory, expect=False)
        assert diagnostic in output, f"incompatible {index}: {output}"
    bindings = [
        ("input wire clk,input wire reset,input wire other", "clock=other,reset=reset", "posedge clk", "clock domains"),
        ("input wire clk,input wire reset", "clock=clk,reset=reset", "negedge clk", "bound rising-edge clock"),
        ("input wire clk", "clock=clk", "posedge clk", "scalar static module inputs"),
        ("input wire clk,input wire reset", "clock=clk,reset=reset,resetn=reset", "posedge clk", "both reset and resetn"),
        ("input wire clk,input wire reset", "clock=(clk + 0),reset=reset", "posedge clk", "scalar static module inputs"),
        ("input bit[2] wire clk,input wire reset", "clock=clk,reset=reset", "posedge clk", "scalar static module inputs"),
        ("input wire clk,input bit[2] wire reset", "clock=clk,reset=reset", "posedge clk", "scalar static module inputs"),
        ("input bit buf clk,input wire reset", "clock=clk,reset=reset", "posedge clk", "scalar static module inputs"),
    ]
    for index, (controls, config, edge, diagnostic) in enumerate(bindings):
        source = directory / f"binding_bad{index}.pigen"
        source.write_text(f"module bad({controls},input uint[32] axi4_stream[{config}] s,"
                          f"output uint[32] axi4_stream[{config}] m);\n"
                          f"always @({edge}) m <= s;\nendmodule\n")
        output = run([ROOT / "pigen", source, "-o", directory / "bad.sv"], directory, expect=False)
        assert diagnostic in output, f"binding {index}: {output}"
    print(f"PASS {len(bad)} invalid declarations, {len(incompatible)} incompatible copies, {len(bindings)} invalid clock/reset bindings")


def child_pin_mapping(directory):
    for family, config in [
            ("axi4_stream", "keep=1,strb=1,last=1,id=4,dest=3,user=7"),
            ("axi4_lite", "addr=40"),
            ("axi4", "addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13")]:
        source = directory / f"child_{family}.pigen"
        output = directory / f"child_{family}.sv"
        transfers = []
        for channel in channels(family, 32):
            selector = "beat" if channel == "t" else channel
            dst, src = ("s", "m") if channel in ("b", "r") else ("m", "s")
            transfers.append(f"{dst}.{selector} <= {src}.{selector};")
        source.write_text(
            f"module native_child(input wire clk,input wire reset,"
            f"input uint[32] {family}[{config}] s,"
            f"output uint[32] {family}[{config}] m);\n"
            "always @(posedge clk) if (!reset) begin\n" + "\n".join(transfers) +
            "\nend\nendmodule\n"
            "module native_parent(input wire clk,input wire reset);\n"
            "native_child child(.clk(clk),.reset(reset));\nendmodule\n")
        run([ROOT / "pigen", source, "-o", output, "--no-diagram"], directory)
        generated = output.read_text().split("native_child child (", 1)[1].split(");", 1)[0]
        assert ".__pigen_axi_channel_" not in generated
        for channel, fields in channels(family, 32).items():
            for endpoint in ("s", "m"):
                for field in [name for name, _ in fields] + ["valid", "ready"]:
                    assert f".{endpoint}_{channel}{field}(" in generated
        run(["verilator", "--lint-only", "--top-module", "native_parent", output,
             ROOT / "rtl/pigen_primitives.sv"], directory)
    print("PASS native AXI child standard field/valid/ready pin mapping for all families")


def child_negatives(directory):
    leaf = ("module native_leaf(input wire clk,input wire reset,"
            "input uint[32] axi4_stream[id=4] s,output uint[32] axi4_stream[id=4] m);\n"
            "always @(posedge clk) if(!reset) m <= s;\nendmodule\n")
    ports = ("input wire clk,input wire reset,input wire other,input wire altreset,"
             "input uint[32] axi4_stream[id=4] s,output uint[32] axi4_stream[id=4] m,"
             "input uint[32] buf plain,input uint[32] axi4_stream[dest=4] wrong")
    cases = [
        (".clk(clk),.reset(reset),.s(plain),.m(m)", "", "complete compatible protocol"),
        (".clk(clk),.reset(reset),.s(wrong),.m(m)", "", "complete compatible protocol"),
        (".clk(clk),.reset(reset),.s(m)", "", "complementary native endpoint"),
        (".clk(other),.reset(reset),.s(s),.m(m)", "", "clock domains and reset bindings"),
        (".clk(clk),.reset(altreset),.s(s),.m(m)", "", "clock domains and reset bindings"),
        (".clk(clk+other),.reset(reset),.s(s),.m(m)", "", "scalar static parent inputs"),
        (".clk(clk),.reset(reset),.s(s),.s(s),.m(m)", "", None),
        (".clk(clk),.reset(reset),.s(s),.m(m)",
         "always @(posedge clk) if(!reset) child.s <= s;", None),
        (".clk(other),.reset(reset)",
         "always @(posedge clk) if(!reset) child.s <= s;", "clock domains"),
        (".clk(clk),.reset(reset),.s_valid(1'b1)", "", "no single valid/ready"),
    ]
    for index, (connections, body, diagnostic) in enumerate(cases):
        source = directory / f"child_bad_{index}.pigen"
        source.write_text(leaf + f"module parent({ports});\n"
                          f"native_leaf child({connections});\n{body}\nendmodule\n")
        output = run([ROOT / "pigen", source, "-o", directory / "bad.sv"], directory, expect=False)
        if diagnostic:
            assert diagnostic in output, f"child negative {index}: {output}"
    print("PASS native child contract, role, clock/reset and duplicate ownership rejection")


def record_construction_tests(directory):
    bad = [
        ("plain <= uint[32]'('{data:1});", "packed struct or native channel target"),
        ("plain <= uint[32]'((record_t'('{default:0})).unknown);", "unknown struct member"),
        ("m <= uint[36]'(record_t'('{data:s.data,last:s.last}));", "supply every field"),
        ("m <= uint[36]'(record_t'(s.data[0] ? '{data:s.data,default:0} : '{unknown:0,default:0}));", "unknown record field"),
        ("m <= uint[36]'({record_t'('{data:s.data})});", "supply every field"),
        ("m <= uint[36]'({2{record_t'('{data:s.data})}});", "supply every field"),
        ("m <= uint[36]'(record_t'('{data:s.data}) ^ record_t'('{default:0}));", "supply every field"),
        ("m <= s.data[0] ? '{data:s.data} : '{data:s.data,default:0};", "supply every field"),
        ("when (m <= s.data[0] ? '{data:s.data,default:0} : '{unknown:0,default:0}) begin end", "unknown record field"),
        ("m <= '{data:s.data,last:s.last};", "supply every field"),
        ("m <= '{data:s.data,data:s.data,default:0};", "duplicate record field"),
        ("m <= '{data:s.data,unknown:0,default:0};", "unknown record field"),
        ("m <= '{data:s.data,default:0,default:1};", "duplicate record default"),
        ("plain <= '{data:1};", "packed struct or native channel target"),
        ("when (m <= '{data:s.data}) begin end", "supply every field"),
    ]
    for i, (body, diagnostic) in enumerate(bad):
        source = directory / f"record_bad{i}.pigen"
        source.write_text("module bad(input wire clk,input wire reset,"
                          "input uint[32] axi4_stream[last=1,user=3] s,"
                          "output uint[32] axi4_stream[last=1,user=3] m,output uint[32] buf plain);\n"
                          "typedef struct packed {bit [2:0] user;bit last;bit [31:0] data;} record_t;\n"
                          f"always @(posedge clk) if(!reset) begin {body} end\nendmodule\n")
        output = run([ROOT / "pigen", source, "-o", directory / "bad.sv"], directory, expect=False)
        assert diagnostic in output, output
    array = directory / "record_array_bad.pigen"
    array.write_text("module bad(input wire clk,input wire reset);\n"
                     "typedef struct packed {bit [7:0] a;bit [7:0] b;} pair;\n"
                     "pair[2] buf values;always @(posedge clk) if(!reset) values <= '{a:1,b:2};\nendmodule\n")
    output = run([ROOT / "pigen", array, "-o", directory / "bad.sv"], directory, expect=False)
    assert "scalar packed record layout" in output, output
    source, sv, tb, binary = [directory / name for name in ("nested_record.pigen", "nested_record.sv", "nested_record_tb.sv", "nested_record_sim")]
    source.write_text("module nested_record(input wire clk,input wire reset,input uint[8] buf src,output bit[20] buf result);\n"
                      "typedef struct packed { bit [7:0] high; bit signed [7:0] low; } pair;\n"
                      "typedef struct packed { pair inner; bit [3:0] tag; } packet;\n"
                      "typedef packet aliased_packet;\n"
                      "aliased_packet buf value;\n"
                      "always @(posedge clk) if(!reset) begin\n"
                      "value <= aliased_packet'('{tag:5,inner:(src[0] ? '{low:-int[8]'(src),high:src} : '{high:src,low:int[8]'(src)})});\n"
                      "result <= {value.inner.high,value.inner.low,value.tag};\nend\nendmodule\n")
    run([ROOT / "pigen", source, "-o", sv], directory)
    run(["verilator", "--lint-only", "--top-module", "nested_record", sv, ROOT / "rtl/pigen_primitives.sv"], directory)
    tb.write_text("module tb;logic clk=0,reset=1;always #5 clk=~clk;logic[7:0] src;logic src_valid=0,src_ready;"
                  "logic[19:0] result;logic result_valid,result_ready=0;integer sent=0,got=0,seed=453;\n"
                  "nested_record dut(.*);\n"
                  "initial begin repeat(3) @(negedge clk);#1;reset=0;end\n"
                  "always @(negedge clk) begin src_valid=!reset && sent<256;src=8'(sent);result_ready=!reset && (($random(seed)&7)>2);end\n"
                  "always @(posedge clk) if(!reset) begin\n"
                  "if(src_valid && src_ready) sent=sent+1;\n"
                  "if(result_valid && result_ready) begin if(result !== {8'(got),8'((got&1) ? -got : got),4'd5}) $fatal(1,\"nested record order/type\");got=got+1;end\n"
                  "if(got==256) begin $display(\"PASS nested record\");$finish;end end\n"
                  "initial begin #30000;$fatal(1,\"record timeout\");end endmodule\n")
    run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, tb, ROOT / "rtl/pigen_primitives.sv"], directory)
    assert "PASS nested record" in run(["vvp", binary], directory)
    print("PASS named record diagnostics and nested typed records through stalls")


def boundary_fabric_negatives(root):
    header = 'module boundary_bad(input wire clk,input wire reset,input uint[32] axi4_lite s,input uint[32] axi4_lite t,output uint[32] axi4_lite m);\n'
    cases = [
        ('source-reuse', 'fabric begin s -> m; s -> m; endfabric', 'another fabric consumer'),
        ('direct-destination', 'fabric begin s > m; t -> m; endfabric', 'direct AXI fabric destination'),
        ('reversed', 'fabric begin m -> s; endfabric', 'initiator-to-target'),
        ('self', 'fabric begin s -> s; endfabric', 'initiator-to-target'),
        ('ordinary', 'uint[32] buf scalar; fabric begin scalar -> m; endfabric', 'compatible native'),
        ('local-owner', 'always @(posedge clk) if(!reset) m.aw <= s.aw; fabric begin s -> m; endfabric', 'another transfer owner'),
    ]
    for name, body, diagnostic in cases:
        directory = root / ('boundary-negative-' + name)
        directory.mkdir()
        source, output = directory / 'bad.pigen', directory / 'bad.sv'
        source.write_text(header + body + '\nendmodule\n')
        output.write_text('preserve output\n')
        result = run([ROOT / 'pigen', source, '-o', output, '--no-diagram'], directory, expect=False)
        assert diagnostic in result, (name, result)
        assert output.read_text() == 'preserve output\n'
    print('PASS native boundary fabric role, contract and ownership diagnostics')


def standalone_fabric_negatives(root):
    base = 'fabric bad(input wire clk,input wire reset,input uint[32] axi4_lite s,output uint[32] axi4_lite m) begin\ns -> m;\nendfabric\n'
    variants = [
        ('missing-end', base.replace('endfabric', ''), 'endfabric'),
        ('bad-destination', base.replace('s -> m;', 's -> missing;'), 'compatible native'),
        ('wrong-direction', base.replace('s -> m;', 'm -> s;'), 'initiator-to-target'),
        ('duplicate-source', base.replace('s -> m;', 's -> m; s -> m;'), 'another fabric consumer'),
        ('wrong-contract', base.replace('axi4_lite m', 'axi4_lite[addr=40] m'), 'compatible native'),
        ('missing-semicolon', base.replace('s -> m;', 's -> m'), 'semicolon'),
    ]
    for name, text, diagnostic in variants:
        directory = root / ('standalone-negative-' + name)
        directory.mkdir()
        source, output = directory / 'bad.pigen', directory / 'bad.sv'
        source.write_text(text); output.write_text('preserve output\n')
        result = run([ROOT / 'pigen', source, '-o', output, '--no-diagram'], directory, expect=False)
        assert diagnostic in result, (name, result)
        assert output.read_text() == 'preserve output\n'
    print('PASS typed standalone fabric syntax, contract, role and ownership diagnostics')


def computed_record_projection_tests(root):
    directory = root / 'computed-record-projections'
    directory.mkdir()
    source, sv, tb, sim = [directory / p for p in ('record.pigen','record.sv','tb.sv','sim')]
    record = "(aliased_pair'('{a:int[8]'(src),b:~src}))"
    value = '{' + f"{record}.a < 8'sd0,{record}.a[uint[3]\'(src[2:0])],{record}.b[uint[3]\'(src[1:0]) +: 4],{record}.b[(uint[3]\'(src[1:0])+3) -: 4]," + "(aliased_pair'('{a:1,b:2})).b}"
    source.write_text('module computed_record(input wire clk,input wire reset,input uint[8] buf src,output bit[18] buf result);\n'
                      'typedef struct packed {bit signed [7:0] a;bit [7:0] b;} pair;\n'
                      'typedef pair first_alias;typedef first_alias aliased_pair;\n'
                      f'always @(posedge clk) if(!reset) result <= {value};\nendmodule\n')
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','computed_record',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    tb.write_text(r"""module tb;
reg clk=0,reset=1;always #5 clk=~clk;
reg [7:0] src=0;reg src_valid=0,result_ready=0;wire src_ready,result_valid;wire [17:0] result;
computed_record dut(.*);
integer sent=0,got=0,cycle=0,seed=7419;
function [17:0] expected(input integer n);
 expected={1'(n>>7),1'(n>>(n&7)),4'((~n)>>(n&3)),4'((~n)>>(n&3)),8'd2};
endfunction
initial begin repeat(3)@(negedge clk);reset=0;end
always @(negedge clk)begin src_valid=!reset&&sent<256;src=8'(sent);result_ready=!reset&&($random(seed)&7)>2;end
always @(posedge clk)begin
 cycle=cycle+1;
 if(!reset)begin
  if(src_valid&&src_ready)sent=sent+1;
  if(result_valid&&result_ready)begin
   if(result!==expected(got))$fatal(1,"computed record %0d got %h expected %h",got,result,expected(got));
   got=got+1;
  end
  if(got==256)begin $display("PASS computed record projections");$finish;end
 end
 if(cycle>5000)$fatal(1,"computed record timeout");
end
endmodule
""")
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    assert 'PASS computed record projections' in run(['vvp',sim],directory)
    print('PASS computed record aliases, signed members, runtime slices and constant projections')


def computed_record_resize_test(root):
    directory = root / 'computed-record-resize'
    directory.mkdir()
    previous = root / 'computed-record-projections'
    text = (previous/'record.pigen').read_text()
    first=text.index('result <= ')+len('result <= ');last=text.index(';',first)
    expression="bit[18]'((aliased_pair'('{a:int[8]'(src),b:~src})).a[uint[3]'(src[2:0])])"
    source,sv,tb,sim=[directory/p for p in ('record.pigen','record.sv','tb.sv','sim')]
    source.write_text(text[:first]+expression+text[last:])
    bench=(previous/'tb.sv').read_text()
    first=bench.index(' expected=');last=bench.index(';',first)
    tb.write_text(bench[:first]+" expected=18'((n>>(n&7))&1)"+bench[last:])
    run([ROOT/'pigen',source,'-o',sv,'--no-diagram'],directory)
    run(['verilator','--lint-only','--top-module','computed_record',sv,ROOT/'rtl/pigen_primitives.sv'],directory)
    run(['iverilog','-g2012','-s','tb','-o',sim,sv,tb,ROOT/'rtl/pigen_primitives.sv'],directory)
    assert 'PASS computed record projections' in run(['vvp',sim],directory)
    print('PASS explicit projected-record resize through 256 stalled transfers')


def data_typedef_emission_tests(root):
    # Helpers retain the authored name; flattened native pins alone cannot detect
    # a missing typedef. Values check layout and signed member interpretation.
    forms = {
        "unsigned": ("typedef uint[32] word_t;", "value", "0"),
        "signed": ("typedef int[32] word_t;", "value", "value<0"),
        "record": ("typedef struct packed {uint[16] hi;int[16] lo;} word_t;",
                   "{value.hi,value.lo}", "value.lo<0"),
        "inline": ("typedef struct packed {uint[16] hi;struct packed {uint[8] tag;int[8] value;} low;} word_t;",
                   "{value.hi,value.low.tag,value.low.value}", "value.low.value<0"),
        "nested": ("typedef struct packed {uint[8] tag;int[8] value;} low_t;"
                   "typedef struct packed {uint[16] hi;low_t low;} word_t;",
                   "{value.hi,value.low.tag,value.low.value}", "value.low.value<0"),
    }
    for scope in ("global", "module"):
        for name,(typedefs,echo,negative) in forms.items():
            directory=root/f"typedef_{scope}_{name}"
            directory.mkdir()
            helpers=("function automatic integer width();return $bits(word_t);endfunction "
                     f"function automatic bit[31:0] echo(input word_t value);return {echo};endfunction "
                     f"function automatic bit negative(input word_t value);return {negative};endfunction ")
            source=((typedefs if scope=="global" else "")+
                    "module dtype_helper(output wire[31:0] bits_out);"+
                    (typedefs if scope=="module" else "")+helpers+
                    "assign bits_out=width();endmodule")
            pigen=directory/"helper.pigen";pigen.write_text(source)
            sv=directory/"helper.sv"
            run([ROOT/"pigen",pigen,"-o",sv],directory)
            run(["verilator","--lint-only","--top-module","dtype_helper",sv],directory)
            sign={"unsigned":"0", "signed":"value[31]", "record":"value[15]", "nested":"value[7]", "inline":"value[7]"}[name]
            tb=directory/"tb.sv"
            tb.write_text("module tb;wire[31:0] bits_out;reg[31:0] value;integer i;"
                          "dtype_helper dut(bits_out);initial begin #1;"
                          "if(bits_out!==32)$fatal(1,\"typedef width\");"
                          "for(i=0;i<257;i=i+1)begin value=32'h9e3779b9*i;"
                          f"if(dut.echo(value)!==value || dut.negative(value)!=={sign})"
                          "$fatal(1,\"typedef layout/sign\");end "
                          "$display(\"PASS DATA typedef helper\");$finish;end endmodule")
            binary=directory/"sim"
            run(["iverilog","-g2012","-s","tb","-o",binary,sv,tb],directory)
            assert "PASS DATA typedef" in run(["vvp",binary],directory)
    directory=root/"typedef_parameterized"
    directory.mkdir()
    pigen=directory/"helper.pigen";sv=directory/"helper.sv";tb=directory/"tb.sv"
    pigen.write_text("module dtype_child #(parameter W=16)(output wire[31:0] n);"
                     "typedef struct packed{uint[W] hi;int[W] lo;} word_t;"
                     "function automatic integer width();return $bits(word_t);endfunction "
                     "assign n=width();endmodule "
                     "module dtype_parent(output wire[31:0] a,output wire[31:0] b,output wire[31:0] c);"
                     "dtype_child #(.W(8)) x(.n(a));dtype_child y(.n(b));dtype_child #(.W(24)) z(.n(c));endmodule")
    run([ROOT/"pigen",pigen,"-o",sv],directory)
    run(["verilator","--lint-only","--top-module","dtype_parent",sv],directory)
    tb.write_text('module tb;wire[31:0] a,b,c;dtype_parent dut(a,b,c);initial begin #1;'
                  'if(a!==16 || b!==32 || c!==48)$fatal(1,"parameterized typedef widths");'
                  '$display("PASS parameterized DATA typedefs");$finish;end endmodule')
    run(["iverilog","-g2012","-s","tb","-o",directory/"sim",sv,tb],directory)
    assert "PASS parameterized" in run(["vvp",directory/"sim"],directory)
    for family,storage in [("axi4_stream","buf"),("axi4_lite","fifo[3]"),("axi4","skid")]:
        for mode in (False, 'fabric_routed'):
            for dtype in ("named","inline","inline_macro","inline_retained","inline_mutable"):
                directory=root/f"typedef_pins_{family}_{mode}_{dtype}"
                directory.mkdir()
                case(directory,family,32,storage,hierarchy=mode,data_typedef=dtype)
            directory=root/f"typedef_patterns_{family}_{mode}"
            directory.mkdir()
            case(directory,family,32,storage,hierarchy=mode,data_typedef="inline",construction="nested_data")
    print("PASS DATA typedefs: ten retained helper profiles and thirty-six native pin profiles")


def static_record_preservation_tests(root):
    directory=root/"retained_static_records";directory.mkdir()
    source=directory/"record.pigen";sv=directory/"record.sv";tb=directory/"tb.sv"
    source.write_text("typedef struct packed {logic[3:0] lead;logic signed[-2:1] signed_asc;"
                      "struct packed {logic[7:0] data;logic[2:0] flag;} body;logic[2:1][4:3] grid;} record_t;"
                      "module static_records(input record_t wire x,output record_t wire copied,output logic[7:0] wire probe,output logic[7:0] wire typed_probe);"
                      "record_t wire local_record;assign local_record=x;"
                      "function automatic logic negative(input record_t value);return value.signed_asc<0;endfunction "
                      "assign copied=local_record;"
                      "assign probe={local_record.lead[3],local_record.signed_asc[-2],local_record.signed_asc[1],"
                      "local_record.body.data[7],local_record.body.flag[1],local_record.grid[1][3],"
                      "negative(local_record),local_record.body.data==8'ha5};"
                      "assign typed_probe={peek(local_record.lead[3]),peek(local_record.signed_asc[-2]),"
                      "peek(local_record.signed_asc[1]),peek(local_record.body.data[7]),peek(local_record.body.flag[1]),"
                      "peek(local_record.grid[1][3]),peek(local_record.signed_asc)<0,peek(local_record.body.data)==8'ha5};endmodule")
    run([ROOT/"pigen",source,"-o",sv],directory)
    # Ascending ranges intentionally trigger fatal style lint in native SV too.
    # Require that parity; do not disable the diagnostic to exercise the layout.
    oracle=directory/"oracle.sv"
    oracle.write_text(re.sub(r"peek\(([^()]*)\)",r"(\1)",source.read_text().replace("record_t wire","record_t").replace("logic[7:0] wire","logic[7:0]")))
    for candidate in (oracle,sv):
        lint=run(["verilator","--lint-only","--top-module","static_records",candidate],directory,expect=False)
        assert set(re.findall(r"%Warning-([A-Z0-9_]+)",lint))=={"ASCRANGE"},lint
    tb.write_text("module tb;reg[22:0] x;wire[22:0] copied;wire[7:0] probe,typed_probe;reg[7:0] expected;integer i;"
                  "static_records dut(.*);initial begin for(i=0;i<1024;i=i+1)begin x=23'h5e3779*i;"
                  "if(i%7==0)x[i%23]=1'bx;if(i%11==0)x[(i+3)%23]=1'bz;#1;"
                  "expected={x[22],x[18],x[15],x[14],x[5],x[0],$signed(x[18:15])<0,x[14:7]==8'ha5};"
                  "if(copied!==x || probe!==expected || typed_probe!==expected)$fatal(1,\"retained record layout/sign/state\");end "
                  "$display(\"PASS retained static records: 1024 independent signed/X/Z/range values\");$finish;end endmodule")
    run(["iverilog","-g2012","-s","tb","-o",directory/"sim",sv,tb],directory)
    assert "PASS retained static" in run(["vvp",directory/"sim"],directory)
    base_source=source.read_text();base_tb=tb.read_text()
    for kind in ("packed","unpacked"):
        directory=root/f"retained_{kind}_records";directory.mkdir()
        if kind=="packed":
            variant=base_source.replace("record_t wire local_record;assign local_record=x;",
                                        "record_t[1:0] wire local_record;assign local_record={x,x};")
            index="1"
        else:
            variant=base_source.replace("record_t wire local_record;assign local_record=x;",
                                        "record_t wire local_record[-1:0];assign local_record[-1]=x;assign local_record[0]=x;")
            index="-1"
        variant=variant.replace("local_record.",f"local_record[{index}].").replace("negative(local_record)",f"negative(local_record[{index}])").replace("copied=local_record;","copied=local_record[0];")
        bench=base_tb
        if kind=="unpacked":
            # Icarus aborts on these ordinary SV unpacked-record field reads;
            # use Verilator with known values, retaining X/Z coverage above.
            variant=variant.replace("logic signed[-2:1]","logic signed[1:-2]")
            bench=bench.replace("x=23'h5e3779*i;","x=23'(23'h5e3779*i);").replace("if(i%7==0)x[i%23]=1'bx;if(i%11==0)x[(i+3)%23]=1'bz;","").replace("expected={x[22],x[18],x[15]","expected={x[22],x[15],x[18]").replace("signed/X/Z/range values","known signed/range values")
        source=directory/"record.pigen";sv=directory/"record.sv";tb=directory/"tb.sv";oracle=directory/"oracle.sv"
        source.write_text(variant);tb.write_text(bench)
        oracle.write_text(re.sub(r"peek\(([^()]*)\)",r"(\1)",re.sub(r"\bwire\s+","",variant)))
        run([ROOT/"pigen",source,"-o",sv],directory)
        for candidate in (oracle,sv):
            lint=run(["verilator","--lint-only","--top-module","static_records",candidate],directory,expect=kind=="unpacked")
            if kind=="packed":
                assert set(re.findall(r"%Warning-([A-Z0-9_]+)",lint))=={"ASCRANGE"},lint
        if kind=="packed":
            run(["iverilog","-g2012","-s","tb","-o",directory/"sim",sv,tb],directory)
            output=run(["vvp",directory/"sim"],directory)
        else:
            run(["verilator","--binary","--timing","--top-module","tb","--Mdir",directory/"obj","-o",directory/"sim","-j","2",sv,tb],directory)
            output=run([directory/"sim"],directory)
        assert "PASS retained static" in output
    for kind,declaration in [("packed","record_t[1:0] wire local_record;assign local_record={x,x};"),
                             ("unpacked","record_t wire local_record[-1:0];assign local_record[-1]=x;assign local_record[0]=x;")]:
        directory=root/f"unindexed_{kind}_record";directory.mkdir()
        source=directory/"bad.pigen";sv=directory/"bad.sv"
        source.write_text(base_source.replace("record_t wire local_record;assign local_record=x;",declaration))
        sv.write_text("preserve output\n")
        diagnostic=run([ROOT/"pigen",source,"-o",sv],directory,expect=False)
        assert "struct member" in diagnostic or "scalar record" in diagnostic,diagnostic
        assert sv.read_text()=="preserve output\n"


def ordered_record_projection_tests(root):
    directory=root/"ordered_record_projections";directory.mkdir()
    source=directory/"record.pigen";sv=directory/"record.sv";tb=directory/"tb.sv"
    text=("typedef struct packed {logic[3:0] lead;logic signed[1:-2] signed_range;"
          "struct packed {logic[7:0] data;logic[2:0] flag;} body;logic[2:1][4:3] grid;} record_t;\n")
    for kind in ("scalar","packed","unpacked"):
        declaration,entries,target={
            "scalar":("record_t temp;","temp=x;","temp"),
            "packed":("record_t[1:0] temp;","temp={x,x};","temp[1]"),
            "unpacked":("record_t temp[-1:0];","temp[-1]=x;temp[0]=x;","temp[-1]")}[kind]
        text+=(f"module mutate_{kind}(input record_t wire x,output record_t wire result);"
               f"always_comb begin {declaration}int[5] coordinate;{entries}"
               f"coordinate=int[5]'({target}.body.flag[1:0])-2;"
               f"{target}.signed_range[coordinate]=~{target}.signed_range[coordinate];"
               f"for(int[4] i=1;i<=2;i++)begin {target}.grid[i][int[4]'({target}.body.flag[0])+3]="
               f"~{target}.grid[i][int[4]'({target}.body.flag[0])+3];end result={target};end endmodule\n")
    source.write_text(text)
    run([ROOT/"pigen",source,"-o",sv],directory)
    run(["verilator","--lint-only","--top-module","mutate_"+kind,sv],directory)
    tb.write_text("module tb;reg[22:0] x;wire[22:0] scalar_result,packed_result,unpacked_result;reg[22:0] expected;integer i;"
                  "mutate_scalar a(x,scalar_result);mutate_packed b(x,packed_result);mutate_unpacked c(x,unpacked_result);"
                  "initial begin for(i=0;i<1024;i=i+1)begin x=23'(23'h5e3779*i);#1;"
                  "expected=x^(23'h1<<(15+32'(x[5:4])))^(23'h5<<x[4]);"
                  "if(scalar_result!==expected || packed_result!==expected || unpacked_result!==expected)"
                  "$fatal(1,\"ordered record coordinates\");end $display(\"PASS 1024 ordered record loops scalar/packed/unpacked\");$finish;end endmodule")
    run(["verilator","--binary","--timing","--top-module","tb","--Mdir",directory/"obj","-o",directory/"sim","-j","2",sv,tb],directory)
    assert "PASS 1024 ordered" in run([directory/"sim"],directory)


def main():
    root = Path(tempfile.mkdtemp(prefix="pigen-native-protocol-"))
    try:
        for index, config in enumerate([
                ("axi4_stream", 24, "buf", False),
                ("axi4_stream", 32, "skid", True),
                ("axi4_lite", 32, "buf", False),
                ("axi4_lite", 64, "fifo[3]", False),
                ("axi4", 8, "buf", False),
                ("axi4", 256, "skid", False),
                ("axi4", 1024, "buf", False),
                ("axi4_stream", 32, "buf", False, "high"),
                ("axi4_stream", 32, "skid", True, "low"),
                ("axi4_lite", 64, "fifo[3]", False, "late_low"),
                ("axi4", 64, "skid", False, "low")]):
            directory = root / str(index)
            directory.mkdir()
            case(directory, *config)
        standalone_fabric_negatives(root)
        boundary_fabric_negatives(root)
        negatives(root)
        alias_negatives(root)
        alias_shadow_tests(root)
        forward_alias_tests(root)
        package_type_tests(root)
        package_import_tests(root)
        package_export_tests(root)
        ordinary_package_tests(root)
        package_subroutine_tests(root)
        retained_namespace_tests(root)
        forward_package_call_tests(root)
        qualified_if_tests(root)
        qualified_if_channel_tests(root)
        qualified_if_fsm_tests(root)
        qualified_if_fsm_oracles(root)
        qualified_if_combinational_tests(root)
        four_state_channel_tests(root)
        signed_four_state_channel_tests(root)
        void_stream_tests(root)
        packed_data_alias_tests(root)
        data_typedef_emission_tests(root)
        static_record_preservation_tests(root)
        ordered_record_projection_tests(root)
        named_type_example_tests(root)
        external_axi_tests(root)
        child_pin_mapping(root)
        child_negatives(root)
        record_construction_tests(root)
        computed_record_projection_tests(root)
        computed_record_resize_test(root)
        for family, storage in [("axi4_stream", "buf"), ("axi4_lite", "fifo[3]"), ("axi4", "skid")]:
            for ordinary,mode in [("qualified","aggregate"),("header","fabric_direct"),("unit","fabric_routed"),
                                  ("record","aggregate"),('transitive', 'fabric_routed')]:
                directory=root/f"ordinary_{ordinary}_{family}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",hierarchy=mode,ordinary_package=ordinary)
            for ordinary,mode in [("mixed","aggregate"),('mixed_transitive', 'fabric_routed'),('retained_export', 'fabric_routed'),('forward_call', 'fabric_routed')]:
                directory=root/f"ordinary_{ordinary}_{family}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",hierarchy=mode,ordinary_package=ordinary)
            directory=root/f"ordinary_parameterized_{family}"
            directory.mkdir()
            case(directory,family,32,storage,binding="low",hierarchy="aggregate",
                 alias="package_parameterized",ordinary_package="qualified")
            directory=root/f"native_package_subroutine_{family}"
            directory.mkdir()
            case(directory,family,32,storage,binding="low",hierarchy="aggregate",alias="package_subroutine_parameterized")
            for kind,mode in [("named","aggregate"),("wildcard","fabric_routed"),("all_parameterized","aggregate"),("early_parameterized","aggregate")]:
                directory=root/f"export_{kind}_{family}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",hierarchy=mode,alias="package_export_"+kind)
            for kind in ("procedural_import", "procedural_import_type"):
                directory=root/f"{kind}_{family}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",construction=kind)
            for kind, mode in [("unit","aggregate"),("explicit","fabric_direct"),("header_parameterized","aggregate"),
                               ("header_explicit","fabric_routed"),('body', 'fabric_routed'),("package","aggregate")]:
                directory = root/f"import_{family}_{kind}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",hierarchy=mode,alias="package_import_"+kind)
            for mode in (False, "aggregate", "fabric_direct", "fabric_routed"):
                directory = root/f"package_{family}_{mode}"
                directory.mkdir()
                case(directory,family,32,storage,binding="low",hierarchy=mode,alias="package")
            directory = root/f"package_{family}_parameterized"
            directory.mkdir()
            case(directory,family,32,storage,binding="low",hierarchy="aggregate",alias="package_parameterized")
            for mode in (False, "aggregate", "fabric_direct"):
                directory = root / f"alias_{mode}_{family}"
                directory.mkdir()
                case(directory, family, 32, storage, binding="low", hierarchy=mode, alias=True)
            for mode in ("parameterized", "parameterized_default"):
                directory = root / f"alias_{mode}_{family}"
                directory.mkdir()
                case(directory, family, 32, "buf" if mode == "parameterized_default" else storage,
                     binding="low", hierarchy="aggregate", alias=mode)
            for mode in ('direct', 'routed'):
                directory = root / f'boundary_{mode}_{family}'
                directory.mkdir()
                case(directory, family, 32, storage, binding='low', fabric=mode)
            for construction in ("inquiry_peek","inquiry_peek_when","inquiry_peek_fsm","inquiry_valid","inquiry_valid_when","inquiry_valid_fsm","inquiry_ready","inquiry_ready_when","inquiry_ready_fsm","inquiry_accepts","inquiry_accepts_when","inquiry_accepts_fsm","query_assign", "query_fields", "query_when", "query_fsm", "bits_fields", "bits_when", "bits_fsm", "zero_fields", "zero_when", "zero_fsm", "named", "when", "default", "fill_one", "fill_x", "fill_z", "fill_conditional", "fill_cast_one", "fill_cast_x", "fill_cast_conditional", "fill_when", "fill_fsm", "compound_fill", "compound_fill_when", "compound_fill_fsm", "selected_fill", "selected_fill_when", "selected_fill_fsm", "called_fill", "called_fill_when", "called_fill_fsm", "called_fill_peek", "fsm", "conditional", "conditional_mixed", "conditional_nested", "conditional_when", "conditional_fsm", "cast", "cast_when", "cast_fsm", "cast_conditional", "cast_concat", "cast_bitwise", "cast_replication", "cast_projected", "peek_projected"):
                directory = root / f"record_{construction}_{family}"
                directory.mkdir()
                case(directory, family, 32, storage, signed=family == "axi4_stream", construction=construction)
            directory = root / f"hierarchy_{family}"
            directory.mkdir()
            case(directory, family, 32, storage, binding="low", hierarchy=True)
            for mode in ("aggregate", "positional", "siblings", "inverted", "fabric_direct", "fabric_routed"):
                directory = root / f"{mode}_{family}"
                directory.mkdir()
                case(directory, family, 32, storage, binding="low", hierarchy=mode)
        example = root / "example.sv"
        run([ROOT / "pigen", ROOT / "examples/native_axi_relay.pigen", "-o", example], root)
        for top in ("native_axis_relay", "native_axil_relay", "native_axi4_relay", "native_axil_aresetn_relay", "native_axi4_chain", "native_axi4_merge2", "native_axis_switch2x3"):
            run(["verilator", "--lint-only", "--top-module", top, example,
                 ROOT / "rtl/pigen_primitives.sv"], root)
        print("PASS parameterized native AXI relay examples")
    except BaseException:
        print(f"Native protocol evidence retained in {root}")
        raise
    else:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
