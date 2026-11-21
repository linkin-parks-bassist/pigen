#!/usr/bin/env python3
"""Check that README examples and linked local paths match the compiler."""
from pathlib import Path
import re
import tempfile
from native_protocol_test import ROOT, run

README = ROOT / "README.md"


def compile_and_lint(directory, name, text):
    source = directory / f"{name}.pigen"
    sv = directory / f"{name}.sv"
    source.write_text(text)
    run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", name,
         ROOT / "rtl/pigen_primitives.sv", sv], directory)
    return sv


def simulate(directory, name, design, tb_text):
    tb = directory / f"{name}_tb.sv"
    binary = directory / f"{name}_sim"
    tb.write_text(tb_text)
    run(["iverilog", "-g2012", "-s", f"{name}_tb", "-o", binary, design, tb,
         ROOT / "rtl/pigen_primitives.sv"], directory)
    return run(["vvp", binary], directory)


def local_links():
    text = README.read_text()
    links = re.findall(r"\]\(([^)]+)\)", text)
    missing = []
    for link in links:
        if link.startswith(("http://", "https://", "#")):
            continue
        target = (ROOT / link).resolve()
        if not target.exists():
            missing.append(link)
    assert not missing, f"README links do not exist: {missing}"


def readme_snippets(directory):
    text = README.read_text()
    declarations = """module readme_declarations #(parameter int lanes = 2)(
    input wire clk, input wire reset);
    bit[32] wire address;
    int[16] buf sample;
    uint[24] port result;
    bit[8] logic tag;
    int[16] fifo[8] pending[lanes];
endmodule
"""
    assert "bit[32] wire address;" in text
    assert "int[16] fifo[8] pending[lanes];" in text
    compile_and_lint(directory, "readme_declarations", declarations)

    begin = """module readme_transfer_begin(
    input wire clk, input wire reset, input uint[32] buf result,
    output uint[32] buf output_packet, output uint[32] fifo[8] history);
    always @(posedge clk) begin
        transfer begin
            output_packet <= result;
            history      <= result;
        end
    end
endmodule
"""
    assert "transfer begin" in text and "history      <= result;" in text
    compile_and_lint(directory, "readme_transfer_begin", begin)

    pipeline = """module readme_pipeline(
    input wire clk, input wire reset,
    input uint[32] buf incoming, output uint[32] buf outgoing);
    always @(posedge clk) if (!reset) begin
        pipeline packet begin
            uint[32] x;

            stage begin x <= incoming; end
            stage begin x <= x + 1; end
            stage begin x <= x * 2; end

            yield x;
        endpipeline

        outgoing <= packet;
    end
endmodule
"""
    assert "always @(posedge clk) if (!reset) begin\n    pipeline packet begin" in text
    compile_and_lint(directory, "readme_pipeline", pipeline)

    stream = """module stream_stage (
    input wire clk,
    input wire reset,
    input uint[32] axi4_stream[keep=1,last=1] s,
    output uint[32] axi4_stream[keep=1,last=1] fifo[4] m
);
    always @(posedge clk) m <= s;
endmodule
"""
    assert "axi4_stream[keep=1,last=1] fifo[4] m" in text
    compile_and_lint(directory, "stream_stage", stream)

    relay = """module readme_relay (
    input wire clk, input wire reset,
    input uint[32] axi4_lite[addr=32] s,
    output uint[32] axi4_lite[addr=32] m
);
    always @(posedge clk) if (!reset) begin
        m.aw <= s.aw;
        m.w  <= s.w;
        s.b  <= m.b;
        m.ar <= s.ar;
        s.r  <= m.r;
    end
endmodule
"""
    for statement in ("m.aw <= s.aw;", "m.w  <= s.w;", "s.b  <= m.b;",
                      "m.ar <= s.ar;", "s.r  <= m.r;"):
        assert statement in text, statement
    compile_and_lint(directory, "readme_relay", relay)


def examples(directory):
    scaled = directory / "scaled.sv"
    run([ROOT / "pigen", ROOT / "examples/scaled_samples.pigen", "-o", scaled,
         "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", "scaled_samples",
         ROOT / "rtl/pigen_primitives.sv", scaled], directory)
    output = simulate(directory, "scaled", scaled, """module scaled_tb;
    logic clk=0, reset=1;
    logic [31:0] incoming, expected[0:2];
    logic incoming_valid=0, outgoing_ready=0;
    wire incoming_ready, outgoing_valid;
    wire [31:0] outgoing;
    integer sent=0, got=0, cycle=0;
    scaled_samples dut(.*);
    always #5 clk=~clk;
    always @(posedge clk) if (!reset) begin
        if (incoming_valid && incoming_ready) sent=sent+1;
        if (outgoing_valid && outgoing_ready) begin
            if (got >= 3 || outgoing !== expected[got])
                $fatal(1, "scaled mismatch %0d", got);
            got=got+1;
        end
    end
    always @(negedge clk) if (!reset) begin
        cycle=cycle+1;
        incoming=32'(sent+1);
        incoming_valid=sent<3 && (cycle%4!=0);
        outgoing_ready=(cycle%5!=0);
    end
    initial begin
        expected[0]=24; expected[1]=27; expected[2]=30;
        repeat(4) @(negedge clk); reset=0;
        wait (got==3); repeat(4) @(posedge clk);
        $display("PASS README scaled_samples 24/27/30"); $finish;
    end
    initial begin #10000; $fatal(1, "scaled timeout"); end
endmodule
""")
    assert "PASS README scaled_samples" in output, output

    pipeline = directory / "pipeline.sv"
    run([ROOT / "pigen", ROOT / "examples/pipeline_fields.pigen", "-o",
         pipeline, "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", "pipeline_fields",
         ROOT / "rtl/pigen_primitives.sv", pipeline], directory)
    output = simulate(directory, "pipeline", pipeline, """module pipeline_tb;
    logic clk=0, reset=1;
    logic [31:0] incoming, expected[0:3];
    logic incoming_valid=0, outgoing_ready=0;
    wire incoming_ready, outgoing_valid;
    wire [31:0] outgoing;
    integer sent=0, got=0, cycle=0;
    pipeline_fields dut(.*);
    always #5 clk=~clk;
    always @(posedge clk) if (!reset) begin
        if (incoming_valid && incoming_ready) begin
            expected[sent]=(32'(sent)+1)*2;
            sent=sent+1;
        end
        if (outgoing_valid && outgoing_ready) begin
            if (got >= 4 || outgoing !== expected[got])
                $fatal(1, "pipeline mismatch %0d got %0d expected %0d",
                       got, outgoing, expected[got]);
            got=got+1;
        end
    end
    always @(negedge clk) if (!reset) begin
        cycle=cycle+1;
        incoming=32'(sent);
        incoming_valid=sent<4 && (cycle%4!=0);
        outgoing_ready=(cycle%5!=0);
    end
    initial begin
        repeat(4) @(negedge clk); reset=0;
        wait (got==4); repeat(4) @(posedge clk);
        $display("PASS README pipeline stages"); $finish;
    end
    initial begin #10000; $fatal(1, "pipeline timeout"); end
endmodule
""")
    assert "PASS README pipeline stages" in output, output

    burst = directory / "burst.sv"
    run([ROOT / "pigen", ROOT / "examples/burst_counter.pigen", "-o", burst,
         "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", "burst_counter",
         ROOT / "rtl/pigen_primitives.sv", burst], directory)
    output = simulate(directory, "burst", burst, """module burst_tb;
    logic clk=0, reset=1;
    logic signed [15:0] length, reqs[0:2], expected[0:4];
    logic length_valid=0, outgoing_ready=0;
    wire length_ready, outgoing_valid;
    wire signed [15:0] outgoing;
    integer sent=0, got=0, cycle=0;
    burst_counter dut(.*);
    always #5 clk=~clk;
    always @(posedge clk) if (!reset) begin
        if (length_valid && length_ready) sent=sent+1;
        if (outgoing_valid && outgoing_ready) begin
            if (got >= 5 || outgoing !== expected[got])
                $fatal(1, "burst mismatch %0d got %0d", got, outgoing);
            got=got+1;
        end
    end
    always @(negedge clk) if (!reset) begin
        cycle=cycle+1;
        length=reqs[sent];
        length_valid=sent<3 && (cycle%4!=0);
        outgoing_ready=(cycle%5!=0);
    end
    initial begin
        reqs[0]=3; reqs[1]=2; reqs[2]=0;
        expected[0]=0; expected[1]=1; expected[2]=2;
        expected[3]=0; expected[4]=1;
        repeat(4) @(negedge clk); reset=0;
        wait (sent==3 && got==5); repeat(20) @(posedge clk);
        if (outgoing_valid) $fatal(1, "nonpositive length emitted a value");
        $display("PASS README burst_counter sequence"); $finish;
    end
    initial begin #20000; $fatal(1, "burst timeout"); end
endmodule
""")
    assert "PASS README burst_counter" in output, output


def fabric(directory):
    source = ROOT / "examples/fabric_control.pigen"
    sv = directory / "control.sv"
    diagram = directory / "control.svg"
    run([ROOT / "pigen", source, "-o", sv, "--diagram", diagram], directory)
    assert "<svg" in diagram.read_text()
    run(["verilator", "--lint-only", "--top-module", "control_network",
         ROOT / "rtl/pigen_primitives.sv", sv], directory)
    implicit = directory / "implicit.sv"
    run([ROOT / "pigen", source, "-o", implicit], directory)
    assert (directory / "implicit.sv.svg").exists()


def main():
    with tempfile.TemporaryDirectory(prefix="pigen-readme-") as temporary:
        directory = Path(temporary)
        local_links()
        readme_snippets(directory)
        examples(directory)
        fabric(directory)
        print("PASS README examples, snippets, links and diagrams")


if __name__ == "__main__":
    main()
