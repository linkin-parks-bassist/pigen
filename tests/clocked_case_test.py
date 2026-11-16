#!/usr/bin/env python3
"""Clocked case selection and edge-qualified checks against native SV."""
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
from native_protocol_test import ROOT, run, case


def four_state_cases(root):
    for width in (4, 33, 65, 129):
        for kind in ("case", "casez", "casex"):
            directory = root / f"{kind}_{width}"
            directory.mkdir()
            source, sv, oracle, tb = [directory / name for name in
                                      ("relay.pigen", "relay.sv", "oracle.sv", "tb.sv")]
            # A default before other items must still mean no item matched.
            # Dynamic comma labels and wildcard bits exercise both operands.
            text = (f"module relay(input wire clk,input logic[3:0] gate,"
                    f"input logic signed[{width-1}:0] data,a,b,output logic[7:0] m);"
                    "always @(posedge clk) if(gate) begin "
                    f"{kind}(data) default:m<=8'hc3; a,b:m<=8'h19; "
                    f"{width}'b1z?0:m<=8'h72; endcase end else m<=8'h56;endmodule")
            source.write_text(text)
            oracle.write_text(text.replace("module relay(", "module reference("))
            tb.write_text(f"module tb;logic clk=0;logic[3:0] gate;logic signed[{width-1}:0] data,a,b;"
                          "wire[7:0] m,expected;integer i,j;always #5 clk=~clk;"
                          "relay dut(.*);reference ref_dut(.m(expected),.*);"
                          "initial begin gate=0;data=0;a=0;b=0;"
                          "for(i=0;i<1024;i=i+1)begin @(negedge clk);"
                          f"data={width}'(i);a={width}'(i+1);b={width}'(i+2);"
                          "for(j=0;j<4;j=j+1)begin case((i>>(j*2))&3)"
                          "0:data[j]=0;1:data[j]=1;2:data[j]=1'bx;3:data[j]=1'bz;endcase end "
                          "if(i%3==0)a=data;else begin a[0]=1'bz;a[1]=1'bx;end "
                          "if(i%5==0)b=a;else begin b[2]=1'bx;b[3]=1'bz;end "
                          "case(i%6)0:gate=0;1:gate=2;2:gate=4'bx000;3:gate=4'bz000;"
                          "4:gate=4'bx010;default:gate=1;endcase "
                          "@(posedge clk);#1;if(m!==expected)$fatal(1,"
                          '"clocked case mismatch i=%0d gate=%b data=%h a=%h b=%h got=%h expected=%h",'
                          "i,gate,data,a,b,m,expected);end "
                          '$display("PASS clocked case four-state selection");$finish;end endmodule')
            run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], directory)
            run(["verilator", "--lint-only", "--top-module", "relay", sv], directory)
            binary = directory / "sim"
            run(["iverilog", "-g2012", "-s", "tb", "-o", binary, sv, oracle, tb], directory)
            assert "PASS clocked case" in run(["vvp", binary], directory)
    print("PASS clocked case/casez/casex: twelve four-state native-SV oracles")


def qualifier_checks(root):
    for qualifier in ("unique", "unique0", "priority"):
        directory = root / qualifier
        directory.mkdir()
        source, sv, oracle, tb = [directory / name for name in
                                  ("relay.pigen", "relay.sv", "oracle.sv", "tb.sv")]
        text = ("module relay(input wire clk,input wire reset,input wire gate,"
                "input logic[7:0] selector,a,b,c,input wire[31:0] s,output logic[31:0] m);"
                "always @(posedge clk) begin if(!reset) begin "
                f"unique if(gate) begin {qualifier} case(selector) "
                "a,b:begin m<=s^32'h2468ace0;end c:m<=s^32'h13579bdf;"
                "default:m<=s;endcase end else m<=0;end end endmodule")
        source.write_text(text)
        oracle.write_text(text)
        tb.write_text("module tb;logic clk=0,reset=1,gate=0;logic[7:0] selector=0,a=0,b=0,c=0;"
                      "logic[31:0] s=0;wire[31:0] m;integer i,mode;always #5 clk=~clk;relay dut(.*);"
                      'initial begin if(!$value$plusargs("MODE=%d",mode))mode=0;'
                      "repeat(3)@(negedge clk);reset=0;gate=1;"
                      "for(i=0;i<256;i=i+1)begin selector=8'(i);a=selector;b=selector+8'd1;"
                      "c=selector+8'd2;s=32'h9e3779b9*32'(i+1);"
                      '@(posedge clk);#1;if(m!==(s^32\'h2468ace0))$fatal(1,"qualified case branch mismatch");'
                      "@(negedge clk);end "
                      "if(mode==1)c=a;else if(mode==2)b=a;else if(mode==3)gate=0;"
                      '@(posedge clk);#1;$display("PASS clocked qualified case checks");$finish;end endmodule')
        run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], directory)
        binaries = []
        for tag, design in (("generated", sv), ("native", oracle)):
            binary = directory / (tag + "_sim")
            run(["verilator", "--binary", "--assert", "--timing", "--top-module", "tb", "-j", "2",
                 "--Mdir", directory / (tag + "_obj"), "-o", binary, design, tb], directory)
            binaries.append(binary)
        for mode in (0, 1, 2, 3):
            reference = subprocess.run([binaries[1], f"+MODE={mode}"], cwd=directory,
                                       text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
            with (directory / "commands.log").open("a") as log:
                log.write(reference.stdout)
            expected = reference.returncode == 0
            if mode == 0:
                assert expected, reference.stdout
            output = run([binaries[0], f"+MODE={mode}"], directory, expect=expected)
            assert ("PASS clocked qualified case" in output) if expected else ("Assertion failed" in output), output
    print("PASS clocked qualifiers: native-SV priority, overlap, comma-group and enclosing-if checks")


def native_transport(root):
    for family in ("axi4_stream", "axi4_lite", "axi4"):
        for qualifier in ("unique", "unique0", "priority"):
            for storage,hierarchy,kind in (("buf",False,"case"),
                                           ("fifo[3]","fabric_routed","casez"),
                                           ("skid","fabric_routed","case")):
                for construction in (False,"fsm"):
                    directory=root/f"native_{family}_{qualifier}_{storage}_{hierarchy}_{construction}"
                    directory.mkdir()
                    case(directory,family,32,storage,hierarchy=hierarchy,alias=True,
                         qualifier=qualifier,case_control=kind,construction=construction)


def consuming_selectors(root):
    # A selector read and a payload read from the same channel consume one
    # token. This is the clocked transfer rule; combinational cases need peek.
    for family in ("axi4_stream","axi4_lite","axi4"):
        for storage,hierarchy,qualifier in (("buf",False,"unique"),
                                            ("fifo[3]","fabric_routed","unique0"),
                                            ("skid","fabric_routed","priority")):
            for construction in (False,"fsm"):
                directory=root/f"consuming_{family}_{storage}_{construction}"
                directory.mkdir()
                case(directory,family,32,storage,hierarchy=hierarchy,alias=True,
                     qualifier=qualifier,case_control="case_direct",construction=construction)


def independent_case_reads(root):
    # Every selected transfer needs the selector and the dynamic label as
    # well as its payload. Repeated comma labels still consume one token.
    for kind in ("case", "casez", "casex"):
        directory=root/f"independent_reads_{kind}"
        directory.mkdir()
        source,sv,tb,binary=[directory/name for name in ("relay.pigen","relay.sv","tb.sv","sim")]
        source.write_text("module relay(input wire clk,input wire reset,"
                          "input logic[64:0] buf selector,input logic[64:0] buf label_packet,"
                          "input uint[8] buf payload_packet,output uint[8] buf m);"
                          "always @(posedge clk)if(!reset)begin "+kind+
                          "(selector)label_packet,label_packet:m<=payload_packet;"
                          "default:m<=payload_packet;endcase end endmodule")
        tb.write_text("module tb;logic clk=0,reset=1;logic[64:0] selector=0,label_packet=0;"
                      "logic[7:0] payload_packet=0;logic selector_valid=0,label_packet_valid=0,payload_packet_valid=0;"
                      "wire selector_ready,label_packet_ready,payload_packet_ready,m_valid;wire[7:0] m;"
                      "logic m_ready=0,st=0,lt=0,pt=0;integer sn=0,ln=0,pn=0,got=0,cycles=0;"
                      "always #5 clk=~clk;relay dut(.*);initial begin repeat(3)@(negedge clk);reset=0;end "
                      "always @(negedge clk)if(!reset)begin m_ready=(cycles%11)>4;"
                      "if(!selector_valid||st)begin selector_valid=sn<97&&(cycles%5)!=1;"
                      "selector={1'b1,32'(sn*19),32'(sn*31)};"
                      "if(sn%3==0)selector[64:63]=2'bxz;end "
                      "if(!label_packet_valid||lt)begin label_packet_valid=ln<97&&(cycles%7)>1;"
                      "label_packet={1'b1,32'(ln*19),32'(ln*31)};"
                      "if(ln%4==0)label_packet[64:63]=2'bzx;else if(ln%4==1)label_packet[32]=~label_packet[32];end "
                      "if(!payload_packet_valid||pt)begin payload_packet_valid=pn<97&&(cycles%3)!=0;"
                      "payload_packet=8'(pn*37+11);end end "
                      "always @(posedge clk)if(!reset)begin cycles++;"
                      "st=selector_valid&&selector_ready;lt=label_packet_valid&&label_packet_ready;"
                      "pt=payload_packet_valid&&payload_packet_ready;"
                      'if(st!==lt||st!==pt)$fatal(1,"case reads did not consume atomically");'
                      "if(st)sn++;if(lt)ln++;if(pt)pn++;"
                      "if(m_valid&&m_ready)begin "
                      'if(got>=pn||m!==8\'(got*37+11))$fatal(1,"case payload order/data mismatch");got++;end end '
                      "initial begin wait(got==97);repeat(12)@(posedge clk);#1;"
                      'if(sn!=97||ln!=97||pn!=97||got!=97||m_valid)$fatal(1,"case dependency count mismatch");'
                      '$display("PASS independent case reads: 97 atomic tokens, duplicate labels, 65-bit X/Z and stalls");$finish;end '
                      'initial begin #30000;$fatal(1,"independent case reads timeout");end endmodule')
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        run(["verilator","--lint-only","--top-module","relay",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
        run(["iverilog","-g2012","-s","tb","-o",binary,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
        assert "PASS independent case reads" in run(["vvp",binary],directory)
    print("PASS independent buffered selector/label/payload reads for case/casez/casex",flush=True)


def default_selector_read(root):
    directory=root/"default_selector_read"
    directory.mkdir()
    source,sv,tb,binary=[directory/name for name in ("relay.pigen","relay.sv","tb.sv","sim")]
    source.write_text("module relay(input wire clk,input wire reset,input uint[8] buf select_packet,"
                      "output uint[8] buf m);always @(posedge clk)if(!reset)begin "
                      "case(select_packet)default:m<=90;endcase end endmodule")
    tb.write_text("module tb;logic clk=0,reset=1;logic[7:0] select_packet=0;logic select_packet_valid=0;"
                  "wire select_packet_ready,m_valid;wire[7:0] m;logic m_ready=0,took=0;integer sent=0,got=0,cycles=0;"
                  "always #5 clk=~clk;relay dut(.*);initial begin repeat(3)@(negedge clk);reset=0;end "
                  "always @(negedge clk)if(!reset)begin m_ready=(cycles%7)>2;"
                  "if(!select_packet_valid || took)begin select_packet_valid=sent<33;select_packet=8'(sent);end end "
                  "always @(posedge clk)if(!reset)begin took=select_packet_valid&&select_packet_ready;cycles++;"
                  "if(took)sent++;if(m_valid&&m_ready)begin "
                  'if(got>=sent || m!==8\'h5a)$fatal(1,"default-only selector dependency lost");got++;end end '
                  "initial begin wait(got==33);repeat(12)@(posedge clk);#1;"
                  'if(sent!=33 || got!=33 || m_valid)$fatal(1,"default-only case manufactured tokens");'
                  '$display("PASS default-only case consumes its otherwise ignored selector");$finish;end '
                  'initial begin #10000;$fatal(1,"default-only case timeout");end endmodule')
    run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
    run(["verilator","--lint-only","--top-module","relay",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
    run(["iverilog","-g2012","-s","tb","-o",binary,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
    assert "PASS default-only case" in run(["vvp",binary],directory)


def fsm_checks(root):
    ports=("input wire clk,input wire reset,input wire stop,input wire restart,input wire jump,"
           "input logic[7:0] selector,a,b,c,input wire[31:0] s,output logic[31:0] m")
    for qualifier in ("unique","unique0","priority"):
        directory=root/f"fsm_{qualifier}"
        directory.mkdir()
        source,sv,oracle,tb=[directory/name for name in ("relay.pigen","relay.sv","oracle.sv","tb.sv")]
        arm="begin m<=s;if(jump)goto idle;end"
        first=(f"{qualifier} case(selector) a:{arm} b:{arm} default:{arm} endcase ")
        tail=f"{qualifier} case(selector) a:m<=~s;c:m<=s;default:m<=0;endcase "
        source.write_text(f"module relay({ports});fsm @(posedge clk)begin if(reset)goto run;"
                          "if(stop)goto idle;state run:begin "+first+tail+
                          "end state idle:if(restart)goto run;end endmodule")
        # Verilator 5.020 places unique-case checks after the selected body;
        # disable can jump past them. A live flag independently models goto
        # termination while allowing the entered case's required check.
        reference=first.replace("goto idle;","begin state<=IDLE;live=0;end ")
        oracle.write_text(f"module relay({ports});localparam RUN=1'b0,IDLE=1'b1;logic state;"
                          "always @(posedge clk)begin bit live;live=1;"
                          "if(reset)begin state<=RUN;live=0;end "
                          "if(live && stop)begin state<=IDLE;live=0;end "
                          "if(live && state==RUN)begin "+reference+"if(live)begin "+tail+"end end "
                          "if(live && state==IDLE && restart)state<=RUN;end endmodule")
        tb.write_text("module tb;logic clk=0,reset=1,stop=0,restart=0,jump=1;"
                      "logic[7:0] selector=0,a=0,b=1,c=0;logic[31:0] s=0;wire[31:0] m;integer i,mode;"
                      "always #5 clk=~clk;relay dut(.*);initial begin "
                      'if(!$value$plusargs("MODE=%d",mode))mode=0;repeat(3)@(negedge clk);reset=0;'
                      "for(i=0;i<128;i=i+1)begin selector=8'(i);a=selector;b=a+8'd1;c=a;"
                      "s=32'h9e3779b9*32'(i+1);@(posedge clk);#1;"
                      'if(m!==s)$fatal(1,"case prior-goto transfer mismatch");'
                      "@(negedge clk);stop=1;@(posedge clk);#1;"
                      'if(m!==s)$fatal(1,"case stop did not terminate");'
                      "@(negedge clk);stop=0;restart=1;@(posedge clk);#1;"
                      'if(m!==s)$fatal(1,"inactive case state executed");'
                      "@(negedge clk);restart=0;end "
                      "if(mode==1)b=a;else if(mode==2)jump=0;"
                      '@(posedge clk);#1;$display("PASS FSM case entry and termination checks");$finish;end endmodule')
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        binaries=[]
        for tag,design in (("generated",sv),("native",oracle)):
            binary=directory/(tag+"_sim")
            run(["verilator","--binary","--assert","--timing","--top-module","tb","-j","2",
                 "--Mdir",directory/(tag+"_obj"),"-o",binary,design,tb],directory)
            binaries.append(binary)
        for mode in (0,1,2):
            reference=subprocess.run([binaries[1],f"+MODE={mode}"],cwd=directory,text=True,
                                     stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            with (directory/"commands.log").open("a") as log:log.write(reference.stdout)
            expected=reference.returncode==0
            if mode==0:assert expected,reference.stdout
            output=run([binaries[0],f"+MODE={mode}"],directory,expect=expected)
            assert ("PASS FSM case" in output) if expected else ("Assertion failed" in output),output
    print("PASS FSM cases: native-SV reset, first-goto and entered-check parity")


def control_fills(root):
    for fill in ("0","1","x","z"):
        directory=root/("fill_"+fill)
        directory.mkdir()
        source,sv,oracle,tb=[directory/name for name in ("relay.pigen","relay.sv","oracle.sv","tb.sv")]
        text=("module relay(input wire clk,input logic[3:0] value,output logic[3:0] m);"
              f"always @(posedge clk) if('{fill}) case(value) 4'd2:m<=4'd7;default:m<=4'd9;endcase "
              "else m<=4'd5;endmodule")
        source.write_text(text);oracle.write_text(text.replace("module relay(","module reference("))
        tb.write_text("module tb;logic clk=0;logic[3:0] value=0;wire[3:0] m,expected;integer i;"
                      "always #5 clk=~clk;relay dut(.*);reference ref_dut(.m(expected),.*);"
                      "initial begin for(i=0;i<32;i=i+1)begin @(negedge clk);value=4'(i);"
                      '@(posedge clk);#1;if(m!==expected)$fatal(1,"control fill mismatch");end '
                      '$display("PASS clocked control fill");$finish;end endmodule')
        run([ROOT/"pigen",source,"-o",sv,"--no-diagram"],directory)
        binary=directory/"sim"
        run(["verilator","--lint-only","--top-module","relay",sv],directory)
        run(["iverilog","-g2012","-s","tb","-o",binary,sv,oracle,tb],directory)
        assert "PASS clocked control fill" in run(["vvp",binary],directory)
    print("PASS clocked if unbased control fills against native SV")


def buffered_actions(root):
    directory=root/"buffered_actions"
    directory.mkdir()
    sv,tb,binary=[directory/name for name in ("actions.sv","tb.sv","sim")]
    run([ROOT/"pigen",ROOT/"tests/clocked_case_error.pigen","-o",sv,"--no-diagram"],directory)
    tb.write_text("module tb;logic clk=0,reset=1,sel=0;logic[7:0] source=0;logic source_valid=0;"
                  "wire source_ready,destination_valid;wire[7:0] destination;integer i,consumed=0;"
                  "always #5 clk=~clk;casem dut(.clk(clk),.reset(reset),.sel(sel),.source(source),"
                  ".source_valid(source_valid),.source_ready(source_ready),.destination(destination),"
                  ".destination_valid(destination_valid),.destination_ready(1'b0));"
                  "always @(posedge clk)if(!reset && source_valid && source_ready)consumed++;"
                  "initial begin repeat(3)@(negedge clk);reset=0;"
                  "for(i=0;i<3;i=i+1)begin sel=0;source=8'(i+39);source_valid=1;"
                  "@(posedge clk);#1;if(!destination_valid || destination!==source)"
                  '$fatal(1,"case transfer failed");'
                  "@(negedge clk);@(posedge clk);#1;if(source_ready || !destination_valid || destination!==source)"
                  '$fatal(1,"case storage changed under stall");'
                  "@(negedge clk);case(i)0:sel=1;1:sel=1'bx;default:sel=1'bz;endcase "
                  "@(posedge clk);#1;if(destination_valid || source_ready)"
                  '$fatal(1,"case default failed to invalidate");@(negedge clk);end '
                  'if(consumed!=3)$fatal(1,"case consumed outside selected arm");'
                  '$display("PASS buffered case transfers and validity actions");$finish;end endmodule')
    run(["verilator","--lint-only","--top-module","casem",sv,ROOT/"rtl/pigen_primitives.sv"],directory)
    run(["iverilog","-g2012","-s","tb","-o",binary,sv,tb,ROOT/"rtl/pigen_primitives.sv"],directory)
    assert "PASS buffered case" in run(["vvp",binary],directory)


def main():
    root = Path(tempfile.mkdtemp(prefix="pigen-clocked-case-"))
    print(root, flush=True)
    try:
        buffered_actions(root)
        if "--actions-only" not in sys.argv:
            four_state_cases(root)
            qualifier_checks(root)
            control_fills(root)
            fsm_checks(root)
            native_transport(root)
            consuming_selectors(root)
            default_selector_read(root)
            independent_case_reads(root)
    except BaseException:
        print(f"Clocked case evidence retained in {root}")
        raise
    else:
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
