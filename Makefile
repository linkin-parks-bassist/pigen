CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude

.PHONY: all clean test block-test verify coslice-test validate-test signed-widen-test waveform compiler-waveform mac-waveform biquad-waveform intended-biquad-waveform text-waveform join-waveform fifo-waveform skid-waveform skid-compare-waveform port-waveform bram-waveform guarded-waveform output-waveform output-test clear-test fsm-test

all: pigen

pigen: src/pigen.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/fsm.c src/lexer.c src/util.c include/pigen/model.h include/pigen/blocks.h include/pigen/assignments.h include/pigen/declarations.h include/pigen/procedural.h include/pigen/fsm.h include/pigen/lexer.h include/pigen/util.h
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/fsm.c src/lexer.c src/util.c

test: pigen block-test
	./tests/smoke.sh ./pigen

block-test: pigen
	./tests/blocks_smoke.sh ./pigen

verify: test coslice-test validate-test signed-widen-test waveform compiler-waveform mac-waveform biquad-waveform intended-biquad-waveform join-waveform fifo-waveform skid-waveform port-waveform bram-waveform guarded-waveform output-waveform clear-test fsm-test

coslice-test: pigen
	./pigen tests/coslice.pigen -o /tmp/pigen-coslice.sv
	iverilog -g2012 -o /tmp/pigen-coslice-vvp rtl/pigen_primitives.sv /tmp/pigen-coslice.sv tests/coslice_tb.sv
	vvp /tmp/pigen-coslice-vvp

validate-test: pigen
	./pigen tests/validate.pigen -o /tmp/pigen-validate.sv
	iverilog -g2012 -o /tmp/pigen-validate-vvp rtl/pigen_primitives.sv /tmp/pigen-validate.sv tests/validate_tb.sv
	vvp /tmp/pigen-validate-vvp

signed-widen-test: pigen
	./pigen tests/signed_widen.pigen -o /tmp/pigen-signed-widen.sv
	iverilog -g2012 -o /tmp/pigen-signed-widen-vvp rtl/pigen_primitives.sv /tmp/pigen-signed-widen.sv tests/signed_widen_tb.sv
	vvp /tmp/pigen-signed-widen-vvp

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

intended-biquad-waveform: pigen
	truncate -s 0 examples/df1_biquad_bandpass.vcd
	./pigen examples/intended_biquad.pigen -o examples/intended_biquad.sv
	iverilog -g2012 -s df1_biquad_bandpass_tb -o /tmp/pigen-intended-biquad-vvp rtl/pigen_primitives.sv examples/intended_biquad.sv examples/df1_biquad_bandpass_tb.sv
	vvp /tmp/pigen-intended-biquad-vvp

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
