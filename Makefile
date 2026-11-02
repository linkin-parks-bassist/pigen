CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS		= -lm

# Each C test program is built once per source change; every case runs that one binary.
TEST_BUILD	= build/tests
TEST_HEADERS	= $(wildcard include/pigen/*.h tests/*.h)
SEMANTIC_SOURCES = src/operation.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c
SYNTAX_SOURCES = src/syntax.c src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c

.PHONY: all clean test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test instance-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test output-model-test fabric-test core-language-test pipeline-test pipeline-scope-test pipeline-syntax-test biquad-bank-test rtl-name-test rtl-lower-test route-test route-law-test route-vdo-test route-stall-test pipeline-identity-test pipeline-output-test pipeline-stage-test pipeline-lower-test fsm-lower-test fsm-identity-test instance-identity-test verify coslice-test coslice-typedef-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform text-waveform join-waveform fifo-waveform skid-waveform skid-compare-waveform port-waveform bram-waveform guarded-waveform output-waveform output-test clear-test fsm-test vertical-slice-test sv-emit-test compile-test coslice-xprop-test

all: pigen

pigen: src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS)

test: pigen source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test fabric-test core-language-test pipeline-test fsm-identity-test
	./tests/smoke.sh ./pigen

$(TEST_BUILD)/pigen-source-test: tests/source_test.c src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/source_test.c src/source.c src/util.c

source-test: $(TEST_BUILD)/pigen-source-test
	@rm -f /tmp/pigen-source-test
	@$(TEST_BUILD)/pigen-source-test

$(TEST_BUILD)/pigen-preprocess-test: tests/preprocess_test.c src/preprocess.c src/lexer.c src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/preprocess_test.c src/preprocess.c src/lexer.c src/source.c src/util.c

preprocess-test: $(TEST_BUILD)/pigen-preprocess-test
	@rm -f /tmp/pigen-preprocess-test
	@$(TEST_BUILD)/pigen-preprocess-test

$(TEST_BUILD)/pigen-transfer-type-test: tests/transfer_type_test.c src/transfer_type.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/transfer_type_test.c src/transfer_type.c

transfer-type-test: $(TEST_BUILD)/pigen-transfer-type-test
	@rm -f /tmp/pigen-transfer-type-test
	@$(TEST_BUILD)/pigen-transfer-type-test

$(TEST_BUILD)/pigen-syntax-model-test: tests/syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES)

syntax-model-test: $(TEST_BUILD)/pigen-syntax-model-test
	@rm -f /tmp/pigen-syntax-model-test
	@$(TEST_BUILD)/pigen-syntax-model-test

$(TEST_BUILD)/pigen-fsm-syntax-test: tests/fsm_syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/fsm_syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES)

fsm-syntax-test: $(TEST_BUILD)/pigen-fsm-syntax-test
	@rm -f /tmp/pigen-fsm-syntax-test
	@$(TEST_BUILD)/pigen-fsm-syntax-test

$(TEST_BUILD)/pigen-instance-syntax-test: tests/instance_syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/instance_syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES)

instance-syntax-test: $(TEST_BUILD)/pigen-instance-syntax-test
	@rm -f /tmp/pigen-instance-syntax-test
	@$(TEST_BUILD)/pigen-instance-syntax-test $(T)

$(TEST_BUILD)/pigen-integer-test: tests/integer_test.c $(SEMANTIC_SOURCES) src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/integer_test.c $(SEMANTIC_SOURCES) src/source.c src/util.c

integer-test: $(TEST_BUILD)/pigen-integer-test
	@rm -f /tmp/pigen-integer-test
	@$(TEST_BUILD)/pigen-integer-test

$(TEST_BUILD)/pigen-semantic-test: tests/semantic_test.c $(SEMANTIC_SOURCES) src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/semantic_test.c $(SEMANTIC_SOURCES) src/source.c src/util.c

semantic-test: $(TEST_BUILD)/pigen-semantic-test
	@rm -f /tmp/pigen-semantic-test
	@$(TEST_BUILD)/pigen-semantic-test

$(TEST_BUILD)/pigen-predicate-test: tests/predicate_test.c src/predicate.c $(SEMANTIC_SOURCES) src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/predicate_test.c src/predicate.c $(SEMANTIC_SOURCES) src/source.c src/util.c

predicate-test: $(TEST_BUILD)/pigen-predicate-test
	@rm -f /tmp/pigen-predicate-test
	@$(TEST_BUILD)/pigen-predicate-test

$(TEST_BUILD)/pigen-expression-resolve-test: tests/expression_resolve_test.c src/expression_analysis.c src/expression_resolve.c src/type_resolve.c $(SEMANTIC_SOURCES) src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/expression_resolve_test.c src/expression_analysis.c src/expression_resolve.c src/type_resolve.c $(SEMANTIC_SOURCES) src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c

expression-resolve-test: $(TEST_BUILD)/pigen-expression-resolve-test
	@rm -f /tmp/pigen-expression-resolve-test
	@$(TEST_BUILD)/pigen-expression-resolve-test

$(TEST_BUILD)/pigen-expression-use-test: tests/expression_use_test.c src/expression_use.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/expression_use_test.c src/expression_use.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

expression-use-test: $(TEST_BUILD)/pigen-expression-use-test
	@rm -f /tmp/pigen-expression-use-test
	@$(TEST_BUILD)/pigen-expression-use-test

$(TEST_BUILD)/pigen-resolve-test: tests/resolve_test.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/resolve_test.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

resolve-test: $(TEST_BUILD)/pigen-resolve-test
	@rm -f /tmp/pigen-resolve-test
	@$(TEST_BUILD)/pigen-resolve-test

$(TEST_BUILD)/pigen-rtl-test: tests/rtl_test.c src/rtl.c src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/rtl_test.c src/rtl.c src/source.c src/util.c

rtl-test: $(TEST_BUILD)/pigen-rtl-test
	@rm -f /tmp/pigen-rtl-test
	@$(TEST_BUILD)/pigen-rtl-test

$(TEST_BUILD)/pigen-rtl-name-test: tests/rtl_name_test.c src/rtl_name.c src/source.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/rtl_name_test.c src/rtl_name.c src/source.c src/util.c

rtl-name-test: $(TEST_BUILD)/pigen-rtl-name-test
	@rm -f /tmp/pigen-rtl-name-test
	@$(TEST_BUILD)/pigen-rtl-name-test

$(TEST_BUILD)/pigen-output-model-test: tests/output_test.c src/output.c src/rtl_lower.c src/rtl.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/output_test.c src/output.c src/rtl_lower.c src/rtl.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

output-model-test: $(TEST_BUILD)/pigen-output-model-test
	@rm -f /tmp/pigen-output-model-test
	@$(TEST_BUILD)/pigen-output-model-test $(T)

$(TEST_BUILD)/pigen-rtl-lower-test: tests/rtl_lower_test.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/rtl_lower_test.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c

rtl-lower-test: $(TEST_BUILD)/pigen-rtl-lower-test
	@rm -f /tmp/pigen-rtl-lower-test
	@$(TEST_BUILD)/pigen-rtl-lower-test $(T)

$(TEST_BUILD)/pigen-route-test: tests/route_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/route_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c

route-test: $(TEST_BUILD)/pigen-route-test
	@rm -f /tmp/pigen-route-test
	@$(TEST_BUILD)/pigen-route-test $(T)

$(TEST_BUILD)/pigen-route-law-test: tests/route_law_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/route_law_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c

route-law-test: $(TEST_BUILD)/pigen-route-law-test
	@rm -f /tmp/pigen-route-law-test
	@$(TEST_BUILD)/pigen-route-law-test $(T)

$(TEST_BUILD)/pigen-route-vdo-test: tests/route_validity_domain_ownership_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/route_validity_domain_ownership_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c

route-vdo-test: $(TEST_BUILD)/pigen-route-vdo-test
	@rm -f /tmp/pigen-route-vdo-test
	@$(TEST_BUILD)/pigen-route-vdo-test $(T)

$(TEST_BUILD)/pigen-route-stall-test: tests/route_stall_throughput_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/route_stall_throughput_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c

route-stall-test: $(TEST_BUILD)/pigen-route-stall-test
	@rm -f /tmp/pigen-route-stall-test
	@$(TEST_BUILD)/pigen-route-stall-test $(T)

$(TEST_BUILD)/pigen-pipeline-identity-test: tests/pipeline_identity_test.c src/transfer_type.c $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/pipeline_identity_test.c src/transfer_type.c $(SYNTAX_SOURCES)

pipeline-identity-test: $(TEST_BUILD)/pigen-pipeline-identity-test
	@rm -f /tmp/pigen-pipeline-identity-test
	@$(TEST_BUILD)/pigen-pipeline-identity-test $(T)

$(TEST_BUILD)/pigen-pipeline-output-test: tests/pipeline_output_test.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/pipeline_output_test.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

pipeline-output-test: $(TEST_BUILD)/pigen-pipeline-output-test
	@rm -f /tmp/pigen-pipeline-output-test
	@$(TEST_BUILD)/pigen-pipeline-output-test $(T)

$(TEST_BUILD)/pigen-pipeline-stage-test: tests/pipeline_stage_test.c src/pipeline_stage.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/pipeline_stage_test.c src/pipeline_stage.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

pipeline-stage-test: $(TEST_BUILD)/pigen-pipeline-stage-test
	@rm -f /tmp/pigen-pipeline-stage-test
	@$(TEST_BUILD)/pigen-pipeline-stage-test $(T)

$(TEST_BUILD)/pigen-pipeline-lower-test: tests/pipeline_lower_test.c src/pipeline_lower.c src/pipeline_identity.c src/pipeline_stage.c src/rtl.c src/rtl_lower.c src/predicate.c src/sv_emit.c src/rtl_name.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/pipeline_lower_test.c src/pipeline_lower.c src/pipeline_identity.c src/pipeline_stage.c src/rtl.c src/rtl_lower.c src/predicate.c src/sv_emit.c src/rtl_name.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

pipeline-lower-test: $(TEST_BUILD)/pigen-pipeline-lower-test
	@rm -f /tmp/pigen-pipeline-lower-test
	@$(TEST_BUILD)/pigen-pipeline-lower-test $(T)

$(TEST_BUILD)/pigen-fsm-identity-test: tests/fsm_identity_test.c src/fsm_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/fsm_identity_test.c src/fsm_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

fsm-identity-test: $(TEST_BUILD)/pigen-fsm-identity-test
	@rm -f /tmp/pigen-fsm-identity-test
	@$(TEST_BUILD)/pigen-fsm-identity-test $(T)

$(TEST_BUILD)/pigen-instance-identity-test: tests/instance_identity_test.c src/instance_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/instance_identity_test.c src/instance_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

instance-identity-test: $(TEST_BUILD)/pigen-instance-identity-test
	@rm -f /tmp/pigen-instance-identity-test
	@$(TEST_BUILD)/pigen-instance-identity-test $(T)

$(TEST_BUILD)/pigen-fsm-lower-test: tests/fsm_lower_test.c src/fsm_lower.c src/rtl.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/fsm_lower_test.c src/fsm_lower.c src/rtl.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

fsm-lower-test: $(TEST_BUILD)/pigen-fsm-lower-test
	@rm -f /tmp/pigen-fsm-lower-test
	@$(TEST_BUILD)/pigen-fsm-lower-test $(T)

$(TEST_BUILD)/pigen-instance-lower-test: tests/instance_lower_test.c src/instance_lower.c src/rtl.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/instance_lower_test.c src/instance_lower.c src/rtl.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES)

instance-lower-test: $(TEST_BUILD)/pigen-instance-lower-test
	@rm -f /tmp/pigen-instance-lower-test
	@$(TEST_BUILD)/pigen-instance-lower-test $(T)

$(TEST_BUILD)/pigen-sv-emit-test: tests/sv_emit_test.c src/sv_emit.c src/rtl.c src/source.c src/rtl_name.c src/util.c $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/sv_emit_test.c src/sv_emit.c src/rtl.c src/source.c src/rtl_name.c src/util.c

sv-emit-test: $(TEST_BUILD)/pigen-sv-emit-test
	@rm -f /tmp/pigen-sv-emit-test; \
	status=0; \
	if [ "$(T)" = "t10-no-search" ]; then \
		if grep -nE 'strstr|strchr|pigen_symbol_lookup|pigen_data_type_|pigen_transfer_type_' src/sv_emit.c; then \
			echo "t10-no-search: FAIL: src/sv_emit.c uses a source-text search or semantic-owner lookup"; \
			status=1; \
		else \
			echo "t10-no-search: PASS: no source-text search or semantic-owner lookup in src/sv_emit.c"; \
		fi; \
	else \
		$(TEST_BUILD)/pigen-sv-emit-test $(T) || status=$$?; \
	fi; \
	exit $$status

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

verify: test pipeline-scope-test pipeline-syntax-test biquad-bank-test coslice-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform join-waveform fifo-waveform skid-waveform port-waveform bram-waveform guarded-waveform output-waveform clear-test fsm-test vertical-slice-test

coslice-test: pigen
	./pigen tests/coslice.pigen -o /tmp/pigen-coslice.sv
	iverilog -g2012 -o /tmp/pigen-coslice-vvp rtl/pigen_primitives.sv /tmp/pigen-coslice.sv tests/coslice_tb.sv
	vvp /tmp/pigen-coslice-vvp

coslice-typedef-test: pigen
	./pigen tests/coslice_typedef.pigen -o /tmp/pigen-coslice-typedef.sv
	iverilog -g2012 -o /tmp/pigen-coslice-typedef-vvp rtl/pigen_primitives.sv /tmp/pigen-coslice-typedef.sv tests/coslice_typedef_tb.sv
	vvp /tmp/pigen-coslice-typedef-vvp

coslice-xprop-test: pigen
	./pigen tests/coslice_xprop.pigen -o /tmp/pigen-coslice-xprop.sv
	iverilog -g2012 -o /tmp/pigen-coslice-xprop-vvp rtl/pigen_primitives.sv /tmp/pigen-coslice-xprop.sv tests/coslice_xprop_tb.sv
	vvp /tmp/pigen-coslice-xprop-vvp

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

vertical-slice-test: pigen
	./pigen examples/buf_pipeline.pigen -o /tmp/pigen-vertical-slice.sv
	iverilog -g2012 -o /tmp/pigen-vertical-slice-vvp rtl/pigen_primitives.sv /tmp/pigen-vertical-slice.sv examples/buf_pipeline_tb.sv
	vvp /tmp/pigen-vertical-slice-vvp

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
	verilator --lint-only --top-module fixed_point_mac_vanilla examples/fixed_point_mac_vanilla.sv

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

$(TEST_BUILD)/pigen-compile-test: tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS)

compile-test: $(TEST_BUILD)/pigen-compile-test
	@rm -f /tmp/pigen-compile-test
	@$(TEST_BUILD)/pigen-compile-test

clean:
	rm -rf build
	rm -f pigen
