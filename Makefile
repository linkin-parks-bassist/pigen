CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS		= -lm

.PHONY: all clean test source-test preprocess-test syntax-model-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test fabric-test core-language-test pipeline-test pipeline-scope-test pipeline-syntax-test biquad-bank-test verify coslice-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform text-waveform join-waveform fifo-waveform skid-waveform skid-compare-waveform port-waveform bram-waveform guarded-waveform output-waveform output-test clear-test fsm-test

all: pigen

pigen: src/pigen.c src/blocks.c src/fabric_svg.inc src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/lexer.c src/util.c include/pigen/model.h include/pigen/blocks.h include/pigen/assignments.h include/pigen/declarations.h include/pigen/procedural.h include/pigen/transfer.h include/pigen/pipeline.h include/pigen/fsm.h include/pigen/lexer.h include/pigen/util.h
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/lexer.c src/util.c $(LDLIBS)

test: pigen source-test preprocess-test syntax-model-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test fabric-test core-language-test pipeline-test
	./tests/smoke.sh ./pigen

source-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-source-test tests/source_test.c src/source.c src/util.c
	/tmp/pigen-source-test

preprocess-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-preprocess-test tests/preprocess_test.c src/preprocess.c src/lexer.c src/source.c src/util.c
	/tmp/pigen-preprocess-test

syntax-model-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-syntax-model-test tests/syntax_test.c src/syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c
	/tmp/pigen-syntax-model-test

semantic-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-semantic-test tests/semantic_test.c src/semantic.c src/source.c src/util.c
	/tmp/pigen-semantic-test

predicate-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-predicate-test tests/predicate_test.c src/predicate.c src/semantic.c src/source.c src/util.c
	/tmp/pigen-predicate-test

expression-resolve-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-expression-resolve-test tests/expression_resolve_test.c src/expression_resolve.c src/semantic.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c
	/tmp/pigen-expression-resolve-test

expression-use-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-expression-use-test tests/expression_use_test.c src/expression_use.c src/predicate.c src/resolve.c src/expression_resolve.c src/semantic.c src/syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c
	/tmp/pigen-expression-use-test

resolve-test:
	$(CC) $(CFLAGS) -o /tmp/pigen-resolve-test tests/resolve_test.c src/resolve.c src/expression_resolve.c src/expression_use.c src/predicate.c src/semantic.c src/syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c
	/tmp/pigen-resolve-test

fabric-test: pigen
	./tests/fabric_smoke.sh ./pigen

core-language-test: pigen
	./tests/core_language.sh ./pigen

pipeline-test: pigen
	./pigen tests/pipeline.pigen -o /tmp/pigen-pipeline.sv
	grep -q 'if ((reset)) begin pipe__s0_valid' /tmp/pigen-pipeline.sv
	iverilog -g2012 -o /tmp/pigen-pipeline-vvp rtl/pigen_primitives.sv /tmp/pigen-pipeline.sv tests/pipeline_tb.sv
	vvp /tmp/pigen-pipeline-vvp

pipeline-scope-test: pigen
	./pigen tests/pipeline_scope.pigen -o /tmp/pigen-pipeline-scope.sv
	iverilog -g2012 -o /tmp/pigen-pipeline-scope-vvp rtl/pigen_primitives.sv /tmp/pigen-pipeline-scope.sv tests/pipeline_scope_tb.sv
	vvp /tmp/pigen-pipeline-scope-vvp

pipeline-syntax-test: pigen
	./tests/pipeline_syntax.sh ./pigen

biquad-bank-test: pigen
	./pigen examples/biquad_bank.pigen -o /tmp/pigen-biquad-bank.sv
	iverilog -g2012 -o /tmp/pigen-biquad-bank-vvp rtl/pigen_primitives.sv /tmp/pigen-biquad-bank.sv examples/biquad_bank_tb.sv
	vvp /tmp/pigen-biquad-bank-vvp

verify: test pipeline-scope-test pipeline-syntax-test biquad-bank-test coslice-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform join-waveform fifo-waveform skid-waveform port-waveform bram-waveform guarded-waveform output-waveform clear-test fsm-test

coslice-test: pigen
	./pigen tests/coslice.pigen -o /tmp/pigen-coslice.sv
	iverilog -g2012 -o /tmp/pigen-coslice-vvp rtl/pigen_primitives.sv /tmp/pigen-coslice.sv tests/coslice_tb.sv
	vvp /tmp/pigen-coslice-vvp

slicing-test: pigen
	./pigen tests/slicing_concat.pigen -o /tmp/pigen-slicing-concat.sv
	iverilog -g2012 -o /tmp/pigen-slicing-concat-vvp rtl/pigen_primitives.sv /tmp/pigen-slicing-concat.sv tests/slicing_concat_tb.sv
	vvp /tmp/pigen-slicing-concat-vvp

signal-syntax-test: pigen
	./tests/signal_syntax.sh ./pigen

validate-test: pigen
	./pigen tests/validate.pigen -o /tmp/pigen-validate.sv
	iverilog -g2012 -o /tmp/pigen-validate-vvp rtl/pigen_primitives.sv /tmp/pigen-validate.sv tests/validate_tb.sv
	vvp /tmp/pigen-validate-vvp

signed-widen-test: pigen
	./pigen tests/signed_widen.pigen -o /tmp/pigen-signed-widen.sv
	iverilog -g2012 -o /tmp/pigen-signed-widen-vvp rtl/pigen_primitives.sv /tmp/pigen-signed-widen.sv tests/signed_widen_tb.sv
	vvp /tmp/pigen-signed-widen-vvp

ready-break-test:
	iverilog -g2012 -s ready_break_tb -o /tmp/pigen-ready-break-vvp rtl/pigen_primitives.sv tests/ready_break_tb.sv
	vvp /tmp/pigen-ready-break-vvp

clear-test:
	verilator --binary --top-module fifo_discard_tb --Mdir /tmp/pigen-clear-verilator rtl/pigen_primitives.sv tests/fifo_discard_tb.sv
	/tmp/pigen-clear-verilator/Vfifo_discard_tb

fsm-test: pigen
	./pigen tests/fsm.pigen -o /tmp/pigen-fsm.sv
	verilator --binary --top-module fsm_tb --Mdir /tmp/pigen-fsm-verilator rtl/pigen_primitives.sv /tmp/pigen-fsm.sv tests/fsm_tb.sv
	/tmp/pigen-fsm-verilator/Vfsm_tb

skid-compare-waveform: pigen
	truncate -s 0 examples/skid_compare.vcd
	./pigen examples/skid_compare.pigen -o examples/skid_compare.sv
	verilator --binary --trace --top-module skid_compare_tb --Mdir /tmp/pigen-skid-compare-verilator rtl/pigen_primitives.sv examples/skid_compare.sv examples/skid_compare_tb.sv
	/tmp/pigen-skid-compare-verilator/Vskid_compare_tb

waveform: pigen
	truncate -s 0 examples/buf_pipeline.vcd
	./pigen examples/buf_pipeline.pigen -o examples/buf_pipeline.sv
	verilator --binary --trace --top-module buf_pipeline_tb --Mdir /tmp/pigen-verilator rtl/pigen_primitives.sv examples/buf_pipeline.sv examples/buf_pipeline_tb.sv
	/tmp/pigen-verilator/Vbuf_pipeline_tb

compiler-waveform: pigen
	truncate -s 0 examples/compiler_pipeline.vcd
	./pigen examples/compiler_pipeline.pigen -o examples/compiler_pipeline.sv
	verilator --binary --trace --top-module compiler_pipeline_tb --Mdir /tmp/pigen-compiler-verilator rtl/pigen_primitives.sv examples/compiler_pipeline.sv examples/compiler_pipeline_tb.sv
	/tmp/pigen-compiler-verilator/Vcompiler_pipeline_tb

mac-waveform: pigen
	truncate -s 0 examples/fixed_point_mac.vcd
	./pigen examples/fixed_point_mac.pigen -o examples/fixed_point_mac.sv
	verilator --binary --trace --top-module fixed_point_mac_tb --Mdir /tmp/pigen-mac-verilator rtl/pigen_primitives.sv examples/fixed_point_mac.sv examples/fixed_point_mac_tb.sv
	/tmp/pigen-mac-verilator/Vfixed_point_mac_tb
	verilator --lint-only -Wno-fatal --top-module fixed_point_mac_vanilla examples/fixed_point_mac_vanilla.sv

biquad-waveform: pigen
	truncate -s 0 examples/df1_biquad_bandpass.vcd
	./pigen examples/df1_biquad_bandpass.pigen -o examples/df1_biquad_bandpass.sv
	iverilog -g2012 -o /tmp/pigen-biquad-vvp rtl/pigen_primitives.sv examples/df1_biquad_bandpass.sv examples/df1_biquad_bandpass_tb.sv
	vvp /tmp/pigen-biquad-vvp

text-waveform: pigen
	truncate -s 0 examples/text.vcd
	./pigen examples/text.pigen -o examples/text.sv
	iverilog -g2012 -s text_tb -o /tmp/pigen-text-vvp rtl/pigen_primitives.sv examples/text.sv examples/text_tb.sv
	vvp /tmp/pigen-text-vvp

join-waveform: pigen
	truncate -s 0 examples/join_pipeline.vcd
	./pigen examples/join_pipeline.pigen -o examples/join_pipeline.sv
	verilator --binary --trace --top-module join_pipeline_tb --Mdir /tmp/pigen-join-verilator rtl/pigen_primitives.sv examples/join_pipeline.sv examples/join_pipeline_tb.sv
	/tmp/pigen-join-verilator/Vjoin_pipeline_tb

fifo-waveform: pigen
	truncate -s 0 examples/fifo_pipeline.vcd
	./pigen examples/fifo_pipeline.pigen -o examples/fifo_pipeline.sv
	verilator --binary --trace --top-module fifo_pipeline_tb --Mdir /tmp/pigen-fifo-verilator rtl/pigen_primitives.sv examples/fifo_pipeline.sv examples/fifo_pipeline_tb.sv
	/tmp/pigen-fifo-verilator/Vfifo_pipeline_tb

skid-waveform: pigen
	truncate -s 0 examples/skid_pipeline.vcd
	./pigen examples/skid_pipeline.pigen -o examples/skid_pipeline.sv
	verilator --binary --trace --top-module skid_pipeline_tb --Mdir /tmp/pigen-skid-verilator rtl/pigen_primitives.sv examples/skid_pipeline.sv examples/skid_pipeline_tb.sv
	/tmp/pigen-skid-verilator/Vskid_pipeline_tb

port-waveform: pigen
	truncate -s 0 examples/port_pipeline.vcd
	./pigen examples/port_pipeline.pigen -o examples/port_pipeline.sv
	verilator --binary --trace --top-module port_pipeline_tb --Mdir /tmp/pigen-port-verilator rtl/pigen_primitives.sv examples/port_pipeline.sv examples/port_pipeline_tb.sv
	/tmp/pigen-port-verilator/Vport_pipeline_tb

bram-waveform: pigen
	truncate -s 0 tests/port_bram.vcd
	./pigen tests/port_bram.pigen -o tests/port_bram.sv
	verilator --binary --trace --top-module port_bram_tb --Mdir /tmp/pigen-bram-verilator rtl/pigen_primitives.sv tests/port_bram.sv tests/port_bram_tb.sv
	/tmp/pigen-bram-verilator/Vport_bram_tb

guarded-waveform: pigen
	truncate -s 0 examples/guarded_pipeline.vcd
	./pigen examples/guarded_pipeline.pigen -o examples/guarded_pipeline.sv
	verilator --binary --trace --top-module guarded_pipeline_tb --Mdir /tmp/pigen-guarded-verilator rtl/pigen_primitives.sv examples/guarded_pipeline.sv examples/guarded_pipeline_tb.sv
	/tmp/pigen-guarded-verilator/Vguarded_pipeline_tb

output-test: pigen
	./pigen examples/output_pipeline.pigen -o examples/output_pipeline.sv
	verilator --binary --top-module output_pipeline_tb --Mdir /tmp/pigen-output-verilator rtl/pigen_primitives.sv examples/output_pipeline.sv examples/output_pipeline_tb.sv
	/tmp/pigen-output-verilator/Voutput_pipeline_tb

output-waveform: pigen
	truncate -s 0 examples/output_pipeline.vcd
	./pigen examples/output_pipeline.pigen -o examples/output_pipeline.sv
	verilator --binary --trace --top-module output_pipeline_tb --Mdir /tmp/pigen-output-verilator rtl/pigen_primitives.sv examples/output_pipeline.sv examples/output_pipeline_tb.sv
	/tmp/pigen-output-verilator/Voutput_pipeline_tb

clean:
	rm -f pigen
