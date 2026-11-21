#!/usr/bin/env python3
"""Atomic transfer blocks parse to one co-sliced transfer and fire atomically."""
from pathlib import Path
import tempfile
from native_protocol_test import ROOT, run

TOTAL = 400


def compile_and_lint(directory, name, source_text):
    source = directory / f"{name}.pigen"
    sv = directory / f"{name}.sv"
    source.write_text(source_text)
    run([ROOT / "pigen", source, "-o", sv, "--no-diagram"], directory)
    run(["verilator", "--lint-only", "--top-module", name,
         ROOT / "rtl/pigen_primitives.sv", sv], directory)
    return source, sv


def simulate(directory, name, sv, tb_text):
    tb = directory / f"{name}_tb.sv"
    binary = directory / f"{name}_sim"
    tb.write_text(tb_text)
    run(["iverilog", "-g2012", "-s", f"{name}_tb", "-o", binary, sv, tb,
         ROOT / "rtl/pigen_primitives.sv"], directory)
    return run(["vvp", binary], directory)


def fanout(directory):
    name = "atomic_fanout"
    source, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[32] buf result,
    output uint[32] buf output_packet,
    output uint[32] fifo[8] history
);
    always @(posedge clk) if (!reset) begin
        transfer begin
            output_packet <= result;
            history <= result;
        end
    end
endmodule
""")
    text = sv.read_text()
    assert text.count("assign result_ready =") == 1, text
    ready = [line for line in text.splitlines()
             if line.strip().startswith("assign result_ready =")][0]
    assert "output_packet__pigen_in_ready" in ready, ready
    assert "history__pigen_in_ready" in ready, ready
    output = simulate(directory, name, sv, f"""module {name}_tb;
    logic clk=0, reset=1;
    logic [31:0] result;
    logic result_valid=0, output_packet_ready=0, history_ready=0;
    wire result_ready, output_packet_valid, history_valid;
    wire [31:0] output_packet, history;
    integer sent=0, out_count=0, hist_count=0, cycle=0;
    logic [31:0] expected[0:1023];
    always #5 clk=~clk;
    {name} dut(.*);
    always @(posedge clk) if (!reset) begin
        if (result_valid && result_ready) begin
            if (sent >= 1024) $fatal(1, "result overflow");
            expected[sent]=result;
            sent=sent+1;
            if (output_packet_valid && !output_packet_ready)
                $fatal(1, "result accepted while the output buffer was full");
        end
        if (output_packet_valid && output_packet_ready) begin
            if (out_count >= sent || output_packet !== expected[out_count])
                $fatal(1, "output packet mismatch at %0d", out_count);
            out_count=out_count+1;
        end
        if (history_valid && history_ready) begin
            if (hist_count >= sent || history !== expected[hist_count])
                $fatal(1, "history mismatch at %0d", hist_count);
            hist_count=hist_count+1;
        end
    end
    always @(negedge clk) if (!reset) begin
        cycle=cycle+1;
        result=32'(cycle*32'h9e3779b9+32'h13579bdf);
        result_valid=sent<{TOTAL} && (cycle%5)!=0;
        output_packet_ready=(cycle%3)!=0;
        history_ready=(cycle%7)!=0 || (cycle%11)==0;
    end
    initial begin
        repeat(4) @(negedge clk);
        if (result_ready) $fatal(1, "result_ready during reset");
        if (output_packet_valid || history_valid)
            $fatal(1, "storage valid during reset");
        reset=0;
        wait (out_count=={TOTAL} && hist_count=={TOTAL});
        repeat(8) @(posedge clk);
        if (sent!={TOTAL}) $fatal(1, "result count %0d", sent);
        if (output_packet_valid || history_valid) $fatal(1, "stale outputs");
        $display("PASS atomic fanout: {TOTAL} results, output=%0d history=%0d",
                 out_count, hist_count);
        $finish;
    end
    initial begin #200000; $fatal(1, "timeout"); end
endmodule
""")
    assert f"PASS atomic fanout: {TOTAL} results" in output, output
    assert f"output={TOTAL} history={TOTAL}" in output, output


def guards_and_contexts(directory):
    name = "transfer_block_guard"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input wire enable,
    input uint[16] buf left,
    input uint[16] buf right,
    output uint[16] buf summed,
    output uint[16] buf diffed
);
    always @(posedge clk) if (!reset) begin
        if (enable) begin
            transfer begin
                summed <= left + right;
                diffed <= left - right;
            end
        end
    end
endmodule
""")
    text = sv.read_text()
    ready = [line for line in text.splitlines()
             if line.strip().startswith("assign left_ready =")][0]
    assert "right_valid" in ready, ready
    assert "summed__pigen_in_ready" in ready and "diffed__pigen_in_ready" in ready, ready
    assert text.count("assign left_ready =") == 1
    assert text.count("assign right_ready =") == 1

    name = "transfer_block_when"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[16] buf source,
    output uint[16] buf first_out,
    output uint[16] buf second_out,
    output uint[16] buf history
);
    always @(posedge clk) if (!reset) begin
        when (first_out <= source) begin
            transfer begin
                second_out <= peek(source);
                history <= peek(source) + 1;
            end
        end
    end
endmodule
""")
    text = sv.read_text()
    ready = [line for line in text.splitlines()
             if line.strip().startswith("assign source_ready =")][0]
    assert "first_out__pigen_in_ready" in ready, ready
    assert "second_out__pigen_in_ready" in text
    assert "history__pigen_in_ready" in text

    name = "transfer_block_fsm"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[16] buf source,
    output uint[16] buf first_out,
    output uint[16] buf second_out
);
    fsm @(posedge clk) begin
        state idle: if (valid(source)) begin
            transfer begin
                first_out <= source;
                second_out <= source + 2;
            end
            goto send;
        end
        state send: when (second_out <= source) goto idle;
    end
endmodule
""")
    assert "assign source_ready = " in sv.read_text()

    name = "transfer_block_pipeline"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[16] buf incoming,
    output uint[16] buf outgoing
);
    always @(posedge clk) if (!reset) begin
        pipeline p begin
            uint[16] sum;
            uint[16] delta;
            stage begin
                transfer begin
                    sum <= incoming;
                    delta <= incoming + 1;
                end
            end
            yield sum + delta;
        endpipeline
        outgoing <= p;
    end
endmodule
""")
    assert "pigen_buf #(.PAYLOAD_T(bit [16:0])) p__pigen_buf" in sv.read_text()


def widths(directory):
    name = "transfer_block_widths"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[24] buf source,
    output uint[8] buf low,
    output uint[16] buf high
);
    always @(posedge clk) if (!reset) begin
        transfer begin
            low <= source[23:16];
            high <= source[15:0];
        end
    end
endmodule
""")
    text = sv.read_text()
    assert text.count("assign source_ready =") == 1
    assert "low__pigen_packet_in" in text and "high__pigen_packet_in" in text

    name = "transfer_block_resize"
    compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[24] buf source,
    output uint[8] buf first,
    output uint[8] buf second
);
    always @(posedge clk) if (!reset) begin
        transfer begin
            first <= source;
            second <= source;
        end
    end
endmodule
""")

    name = "transfer_block_placeholder"
    compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[8] buf source,
    output uint[8] buf kept
);
    always @(posedge clk) if (!reset) begin
        transfer begin
            _ <= source;
            kept <= source;
        end
    end
endmodule
""")

    name = "transfer_block_indexed"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[2] handle,
    input uint[8] buf next_state,
    input uint[8] buf result,
    output uint[8] buf packet
);
    reg [7:0] state_mem[4];
    always @(posedge clk) if (!reset) begin
        transfer begin
            state_mem[handle] <= next_state;
            packet <= result;
        end
    end
endmodule
""")
    ready = [line for line in sv.read_text().splitlines()
             if line.strip().startswith("assign next_state_ready =")][0]
    assert "handle_valid" in ready, ready
    assert "result_valid" in ready, ready

    name = "transfer_block_single"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[8] buf source,
    output uint[8] buf destination
);
    always @(posedge clk) if (!reset) begin
        transfer begin
            destination <= source;
        end
    end
endmodule
""")
    assert "assign destination__pigen_packet_in" in sv.read_text()

    name = "transfer_identifier"
    _, sv = compile_and_lint(directory, name, f"""module {name} (
    input wire clk,
    input wire reset,
    input uint[8] buf source,
    output uint[8] buf transfer
);
    always @(posedge clk) if (!reset) transfer <= source;
endmodule
""")
    assert "output bit [7:0] transfer" in sv.read_text()


def diagnostics(directory):
    header = ("module bad(input logic clk,input logic reset,"
              "input bit[8] buf source,output bit[8] buf destination);\n")
    cases = [
        ("always @(posedge clk) begin transfer begin end end endmodule",
         "transfer block requires at least one destination <= source member"),
        ("always @(posedge clk) begin transfer begin destination source; "
         "end end endmodule",
         "transfer block member requires destination <= source"),
        ("always @(posedge clk) begin transfer begin destination <= ; end end "
         "endmodule", "transfer block member requires a source"),
        ("always @(posedge clk) begin transfer begin if (source_valid) "
         "destination <= source; end end endmodule",
         "transfer block permits assignment members only"),
        ("always @(posedge clk) begin transfer begin "
         "destination <= source; destination <= source <= source; end end "
         "endmodule", "transfer block member requires exactly one <= separator"),
        ("always @(posedge clk) begin transfer begin "
         "destination <= source end end endmodule",
         "transfer block member requires a trailing semicolon"),
        ("always @(posedge clk) begin transfer begin "
         "destination <= source; endmodule",
         "transfer block requires a closing end"),
    ]
    source = directory / "bad.pigen"
    output = directory / "bad.sv"
    for body, message in cases:
        source.write_text(header + body)
        result = run([ROOT / "pigen", source, "-o", output], directory,
                     expect=False)
        assert message in result, result
        assert f"{source}:2:" in result, result
        assert not output.exists(), result


def main():
    with tempfile.TemporaryDirectory(prefix="pigen-transfer-block-") as temporary:
        root = Path(temporary)
        for check in (fanout, guards_and_contexts, widths, diagnostics):
            directory = root / check.__name__
            directory.mkdir()
            check(directory)
            print(f"PASS transfer block: {check.__name__}")


if __name__ == "__main__":
    main()
