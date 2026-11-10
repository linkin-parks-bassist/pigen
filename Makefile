CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS		= -lm
SEMANTIC_SOURCES = src/operation.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c
SYNTAX_SOURCES = src/syntax.c src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c

.PHONY: all clean test ordinary-sv-preserve-test concat-dest-test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test output-model-test fabric-test core-language-test pipeline-test pipeline-scope-test pipeline-syntax-test biquad-bank-test rtl-name-test rtl-lower-test route-test route-law-test route-vdo-test route-stall-test fabric-direct-duplicate-error-test clocked-case-error-test pipeline-reset-after-test pipeline-reset-conditional-test sv-integer-keywords-test integer-signal-test byte-type-test two-state-test validate-input-error-test peek-test validate-order-test macro-paste-decl-test macro-stringify-test macro-paste-ref-test macro-paste-test clog2-test pipeline-reset-test function-retain-test concat-placeholder-test struct-write-test pipeline-alias-test fabric-merge-test fabric-merge3-test concat-misaligned-test ternary-literal-test signed-cast-test typed-param-test integer-param-test pipeline-identity-test pipeline-output-test pipeline-stage-test pipeline-lower-test fsm-lower-test fsm-identity-test verify coslice-test coslice-typedef-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform text-waveform join-waveform fifo-waveform skid-waveform skid-compare-waveform port-waveform bram-waveform guarded-waveform output-waveform output-test clear-test fsm-test vertical-slice-test sv-emit-test compile-test coslice-xprop-test

all: pigen

pigen: src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c $(LDLIBS)


test: pigen ordinary-sv-preserve-test typed-param-test integer-param-test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test fabric-test core-language-test pipeline-test
	./tests/smoke.sh ./pigen

source-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-source-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-source-test tests/source_test.c src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-source-test || status=$$?; \
	rm -rf $$dir; exit $$status

preprocess-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-preprocess-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-preprocess-test tests/preprocess_test.c src/preprocess.c src/lexer.c src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-preprocess-test || status=$$?; \
	rm -rf $$dir; exit $$status

transfer-type-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-transfer-type-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-transfer-type-test tests/transfer_type_test.c src/transfer_type.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-transfer-type-test || status=$$?; \
	rm -rf $$dir; exit $$status

syntax-model-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-syntax-model-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-syntax-model-test tests/syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-syntax-model-test || status=$$?; \
	rm -rf $$dir; exit $$status

fsm-syntax-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-fsm-syntax-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-fsm-syntax-test tests/fsm_syntax_test.c src/transfer_type.c $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-fsm-syntax-test || status=$$?; \
	rm -rf $$dir; exit $$status

integer-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-integer-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-integer-test tests/integer_test.c src/predicate.c $(SEMANTIC_SOURCES) src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-integer-test || status=$$?; \
	rm -rf $$dir; exit $$status

semantic-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-semantic-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-semantic-test tests/semantic_test.c src/predicate.c $(SEMANTIC_SOURCES) src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-semantic-test || status=$$?; \
	rm -rf $$dir; exit $$status

predicate-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-predicate-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-predicate-test tests/predicate_test.c src/predicate.c $(SEMANTIC_SOURCES) src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-predicate-test || status=$$?; \
	rm -rf $$dir; exit $$status

expression-resolve-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-expression-resolve-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-expression-resolve-test tests/expression_resolve_test.c src/expression_analysis.c src/expression_resolve.c src/predicate.c src/type_resolve.c $(SEMANTIC_SOURCES) src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-expression-resolve-test || status=$$?; \
	rm -rf $$dir; exit $$status

expression-use-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-expression-use-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-expression-use-test tests/expression_use_test.c src/expression_use.c src/predicate.c src/resolve.c src/pipeline_identity.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-expression-use-test || status=$$?; \
	rm -rf $$dir; exit $$status

resolve-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-resolve-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-resolve-test tests/resolve_test.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-resolve-test || status=$$?; \
	rm -rf $$dir; exit $$status

rtl-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-rtl-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-rtl-test tests/rtl_test.c src/rtl.c src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-rtl-test || status=$$?; \
	rm -rf $$dir; exit $$status

rtl-name-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-rtl-name-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-rtl-name-test tests/rtl_name_test.c src/rtl_name.c src/source.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-rtl-name-test || status=$$?; \
	rm -rf $$dir; exit $$status

output-model-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-output-model-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-output-model-test tests/output_test.c src/output.c src/rtl_lower.c src/rtl.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/predicate.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-output-model-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

rtl-lower-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-rtl-lower-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-rtl-lower-test tests/rtl_lower_test.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-rtl-lower-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

route-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-route-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-route-test tests/route_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-route-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

route-law-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-route-law-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-route-law-test tests/route_law_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-route-law-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

route-vdo-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-route-vdo-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-route-vdo-test tests/route_validity_domain_ownership_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-route-vdo-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

route-stall-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-route-stall-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-route-stall-test tests/route_stall_throughput_test.c src/route.c src/rtl_lower.c src/rtl.c src/source.c src/predicate.c $(SEMANTIC_SOURCES) src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-route-stall-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

pipeline-identity-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-pipeline-identity-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-pipeline-identity-test tests/pipeline_identity_test.c src/transfer_type.c $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-pipeline-identity-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

pipeline-output-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-pipeline-output-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-pipeline-output-test tests/pipeline_output_test.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-pipeline-output-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

pipeline-stage-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-pipeline-stage-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-pipeline-stage-test tests/pipeline_stage_test.c src/pipeline_stage.c src/pipeline_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-pipeline-stage-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

pipeline-lower-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-pipeline-lower-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-pipeline-lower-test tests/pipeline_lower_test.c src/pipeline_lower.c src/pipeline_identity.c src/pipeline_stage.c src/rtl.c src/rtl_lower.c src/predicate.c src/sv_emit.c src/rtl_name.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-pipeline-lower-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

fsm-identity-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-fsm-identity-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-fsm-identity-test tests/fsm_identity_test.c src/fsm_identity.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-fsm-identity-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

fsm-lower-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-fsm-lower-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-fsm-lower-test tests/fsm_lower_test.c src/fsm_lower.c src/rtl.c $(SEMANTIC_SOURCES) $(SYNTAX_SOURCES) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-fsm-lower-test $(T) || status=$$?; \
	rm -rf $$dir; exit $$status

sv-emit-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-sv-emit-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-sv-emit-test tests/sv_emit_test.c src/sv_emit.c src/rtl.c src/source.c src/rtl_name.c src/util.c || { status=1; rm -rf $$dir; exit 1; }; \
	if [ "$(T)" = "t10-no-search" ]; then \
		if grep -nE 'strstr|strchr|pigen_symbol_lookup|pigen_data_type_|pigen_transfer_type_' src/sv_emit.c; then \
			echo "t10-no-search: FAIL: src/sv_emit.c uses a source-text search or semantic-owner lookup"; \
			status=1; \
		else \
			echo "t10-no-search: PASS: no source-text search or semantic-owner lookup in src/sv_emit.c"; \
		fi; \
	else \
		$$dir/pigen-sv-emit-test $(T) || status=$$?; \
	fi; \
	rm -rf $$dir; exit $$status

fabric-test: pigen
	./tests/fabric_smoke.sh ./pigen

ternary-literal-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/ternary_literal.pigen -o $$dir/ternary.sv && \
	iverilog -g2012 -s ternary_literal_tb -o $$dir/ternary \
		rtl/pigen_primitives.sv $$dir/ternary.sv tests/ternary_literal_tb.sv && \
	vvp $$dir/ternary; status=$$?; rm -rf $$dir; exit $$status

signed-cast-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/signed_cast.pigen -o $$dir/cast.sv && \
	iverilog -g2012 -s signed_cast_tb -o $$dir/cast \
		rtl/pigen_primitives.sv $$dir/cast.sv tests/signed_cast_tb.sv && \
	vvp $$dir/cast; status=$$?; rm -rf $$dir; exit $$status

integer-param-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/integer_param.pigen -o $$dir/integer.sv && \
	iverilog -g2012 -s integer_param_tb -o $$dir/integer \
		rtl/pigen_primitives.sv $$dir/integer.sv tests/integer_param_tb.sv && \
	vvp $$dir/integer; status=$$?; rm -rf $$dir; exit $$status

typed-param-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/typed_param.pigen -o $$dir/typed.sv && \
	iverilog -g2012 -s typed_param_tb -o $$dir/typed \
		rtl/pigen_primitives.sv $$dir/typed.sv tests/typed_param_tb.sv && \
	vvp $$dir/typed; status=$$?; rm -rf $$dir; exit $$status

fabric-merge3-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/fabric_merge3.pigen -o $$dir/merge3.sv && \
	iverilog -g2012 -s fabric_merge3_tb -o $$dir/merge3 \
		rtl/pigen_primitives.sv $$dir/merge3.sv tests/fabric_merge3_tb.sv && \
	vvp $$dir/merge3; status=$$?; rm -rf $$dir; exit $$status

fabric-merge-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/fabric_merge.pigen -o $$dir/merge.sv && \
	iverilog -g2012 -s fabric_merge_tb -o $$dir/merge \
		rtl/pigen_primitives.sv $$dir/merge.sv tests/fabric_merge_tb.sv && \
	vvp $$dir/merge && \
	iverilog -g2012 -s fabric_merge_fair_tb -o $$dir/merge-fair \
		rtl/pigen_primitives.sv $$dir/merge.sv tests/fabric_merge_fair_tb.sv && \
	vvp $$dir/merge-fair; status=$$?; rm -rf $$dir; exit $$status

core-language-test: pigen
	./tests/core_language.sh ./pigen

pipeline-test: pigen
	./pigen tests/pipeline.pigen -o /tmp/pigen-pipeline.sv
	grep -q 'value__pigen_packet_in = ((enable == 1 && source_valid)' /tmp/pigen-pipeline.sv
	iverilog -g2012 -o /tmp/pigen-pipeline-vvp rtl/pigen_primitives.sv /tmp/pigen-pipeline.sv tests/pipeline_tb.sv
	vvp /tmp/pigen-pipeline-vvp

pipeline-scope-test: pigen
	./pigen tests/pipeline_scope.pigen -o /tmp/pigen-pipeline-scope.sv
	iverilog -g2012 -o /tmp/pigen-pipeline-scope-vvp rtl/pigen_primitives.sv /tmp/pigen-pipeline-scope.sv tests/pipeline_scope_tb.sv
	vvp /tmp/pigen-pipeline-scope-vvp

fabric-direct-duplicate-error-test: pigen
	if ./pigen tests/fabric_direct_duplicate_error.pigen -o /tmp/fabric-ddup.sv 2>/tmp/fabric-ddup.err; then \
		echo 'duplicate inline direct fabric destination unexpectedly compiled' >&2; exit 1; fi
	grep -q 'direct fabric destination cannot appear in another connection' /tmp/fabric-ddup.err

clocked-case-error-test: pigen
	if ./pigen tests/clocked_case_error.pigen -o /tmp/case-error.sv 2>/tmp/case-error.err; then \
		echo 'structured case process unexpectedly compiled' >&2; exit 1; fi
	grep -q 'structured construct in an unsupported clocked process is not supported' /tmp/case-error.err

ordinary-sv-preserve-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/ordinary_sv_preserve.pigen -o $$dir/ordinary.sv && \
	grep -Fq 'generate' $$dir/ordinary.sv && \
	grep -Fq 'always_comb' $$dir/ordinary.sv && \
	iverilog -g2012 -s ordinary_sv_preserve_tb -o $$dir/ordinary \
		tests/ordinary_sv_preserve_tb.sv $$dir/ordinary.sv && \
	vvp $$dir/ordinary | grep -Fq 'PASS ordinary SV preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

parameterized-instance-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/parameterized_instance.pigen -o $$dir/param-inst.sv && \
	grep -Fq 'child #(.WIDTH(16)) c' $$dir/param-inst.sv && \
	iverilog -g2012 -s parameterized_instance_tb -o $$dir/param-inst \
		rtl/pigen_primitives.sv tests/parameterized_instance_tb.sv $$dir/param-inst.sv && \
	vvp $$dir/param-inst | grep -Fq 'PASS parameterized child instance'; \
	status=$$?; rm -rf $$dir; exit $$status

parameter-width-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/parameter_width.pigen -o $$dir/param.sv && \
	grep -Fq 'input  bit [7:0] a' $$dir/param.sv && \
	iverilog -g2012 -s parameter_width_tb -o $$dir/param \
		rtl/pigen_primitives.sv $$dir/param.sv tests/parameter_width_tb.sv && \
	vvp $$dir/param; status=$$?; rm -rf $$dir; exit $$status

pipeline-reset-after-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/pipeline_reset_after.pigen -o $$dir/reset.sv && \
	iverilog -g2012 -s prafter_tb -o $$dir/reset \
		rtl/pigen_primitives.sv $$dir/reset.sv tests/pipeline_reset_after_tb.sv && \
	vvp $$dir/reset; status=$$?; rm -rf $$dir; exit $$status

pipeline-reset-conditional-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/pipeline_reset_conditional.pigen -o $$dir/reset.sv && \
	iverilog -g2012 -s pipeline_reset_conditional_tb -o $$dir/reset \
		rtl/pigen_primitives.sv $$dir/reset.sv tests/pipeline_reset_conditional_tb.sv && \
	vvp $$dir/reset; status=$$?; rm -rf $$dir; exit $$status

sv-integer-keywords-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/sv_integer_keywords.pigen -o $$dir/svkw.sv && \
	grep -Fq 'input  shortint a' $$dir/svkw.sv && \
	grep -Fq 'input  longint b' $$dir/svkw.sv && \
	grep -Fq 'input  time t' $$dir/svkw.sv && \
	iverilog -g2012 -s sv_integer_keywords_tb -o $$dir/svkw \
		rtl/pigen_primitives.sv $$dir/svkw.sv tests/sv_integer_keywords_tb.sv && \
	vvp $$dir/svkw; status=$$?; rm -rf $$dir; exit $$status

integer-signal-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/integer_signal.pigen -o $$dir/integer.sv && \
	grep -Fq 'input  integer a' $$dir/integer.sv && \
	grep -Fq 'output integer y' $$dir/integer.sv && \
	iverilog -g2012 -s integer_signal_tb -o $$dir/integer \
		rtl/pigen_primitives.sv $$dir/integer.sv tests/integer_signal_tb.sv && \
	vvp $$dir/integer; status=$$?; rm -rf $$dir; exit $$status

byte-type-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/byte_type.pigen -o $$dir/byte.sv && \
	grep -Fq 'input  byte a' $$dir/byte.sv && \
	grep -Fq 'output byte y' $$dir/byte.sv && \
	iverilog -g2012 -s byte_type_tb -o $$dir/byte \
		rtl/pigen_primitives.sv $$dir/byte.sv tests/byte_type_tb.sv && \
	vvp $$dir/byte; status=$$?; rm -rf $$dir; exit $$status

two-state-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/two_state.pigen -o $$dir/two_state.sv && \
	grep -Fq 'input  bit signed [15:0] a' $$dir/two_state.sv && \
	grep -Fq 'input  bit [15:0] b' $$dir/two_state.sv && \
	grep -Fq 'output bit signed [15:0] y' $$dir/two_state.sv && \
	grep -Fq 'output bit [15:0] z' $$dir/two_state.sv; status=$$?; \
	rm -rf $$dir; exit $$status

validate-input-error-test: pigen
	if ./pigen tests/invalidate_input_error.pigen -o /tmp/invalidate-input.sv 2>/tmp/invalidate-input.err; then \
		echo 'input validity action unexpectedly compiled' >&2; exit 1; fi
	grep -q 'cannot modify validity of a module input' /tmp/invalidate-input.err

peek-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/peek.pigen -o $$dir/peek.sv && \
	iverilog -g2012 -s peek_tb -o $$dir/peek \
		rtl/pigen_primitives.sv $$dir/peek.sv tests/peek_tb.sv && \
	vvp $$dir/peek; status=$$?; rm -rf $$dir; exit $$status

validate-order-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/validate_order.pigen -o $$dir/order.sv && \
	iverilog -g2012 -s validate_order_tb -o $$dir/order \
		rtl/pigen_primitives.sv $$dir/order.sv tests/validate_order_tb.sv && \
	vvp $$dir/order; status=$$?; rm -rf $$dir; exit $$status

macro-paste-decl-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/macro_paste_decl.pigen -o $$dir/paste-decl.sv && \
	iverilog -g2012 -s macro_paste_decl_tb -o $$dir/paste-decl \
		rtl/pigen_primitives.sv $$dir/paste-decl.sv tests/macro_paste_decl_tb.sv && \
	vvp $$dir/paste-decl; status=$$?; rm -rf $$dir; exit $$status

macro-stringify-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/macro_stringify.pigen -o $$dir/stringify.sv && \
	iverilog -g2012 -s macro_stringify_tb -o $$dir/stringify \
		$$dir/stringify.sv tests/macro_stringify_tb.sv && \
	vvp $$dir/stringify | grep -q '^hello_world$$'; status=$$?; \
	rm -rf $$dir; exit $$status

macro-paste-ref-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/macro_paste_ref.pigen -o $$dir/paste-ref.sv && \
	iverilog -g2012 -s macro_paste_ref_tb -o $$dir/paste-ref \
		rtl/pigen_primitives.sv $$dir/paste-ref.sv tests/macro_paste_ref_tb.sv && \
	vvp $$dir/paste-ref; status=$$?; rm -rf $$dir; exit $$status

macro-paste-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/macro_paste.pigen -o $$dir/paste.sv && \
	iverilog -g2012 -s macro_paste_tb -o $$dir/paste \
		rtl/pigen_primitives.sv $$dir/paste.sv tests/macro_paste_tb.sv && \
	vvp $$dir/paste; status=$$?; rm -rf $$dir; exit $$status

clog2-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/clog2.pigen -o $$dir/clog2.sv && \
	iverilog -g2012 -s clog2_tb -o $$dir/clog2 \
		rtl/pigen_primitives.sv $$dir/clog2.sv tests/clog2_tb.sv && \
	vvp $$dir/clog2; status=$$?; rm -rf $$dir; exit $$status

pipeline-reset-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/pipeline_reset.pigen -o $$dir/reset.sv && \
	iverilog -g2012 -s pipeline_reset_tb -o $$dir/reset \
		rtl/pigen_primitives.sv $$dir/reset.sv tests/pipeline_reset_tb.sv && \
	vvp $$dir/reset; status=$$?; rm -rf $$dir; exit $$status

function-retain-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/function_retain.pigen -o $$dir/function.sv && \
	iverilog -g2012 -s function_retain_tb -o $$dir/function \
		$$dir/function.sv tests/function_retain_tb.sv && \
	vvp $$dir/function; status=$$?; rm -rf $$dir; exit $$status

concat-placeholder-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/concat_placeholder.pigen -o $$dir/placeholder.sv && \
	iverilog -g2012 -s concat_placeholder_tb -o $$dir/placeholder \
		rtl/pigen_primitives.sv $$dir/placeholder.sv tests/concat_placeholder_tb.sv && \
	vvp $$dir/placeholder; status=$$?; rm -rf $$dir; exit $$status

struct-write-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/struct_member_write.pigen -o $$dir/write.sv && \
	iverilog -g2012 -s struct_member_write_tb -o $$dir/write \
		rtl/pigen_primitives.sv $$dir/write.sv tests/struct_member_write_tb.sv && \
	vvp $$dir/write; status=$$?; rm -rf $$dir; exit $$status

pipeline-alias-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/pipeline_alias.pigen -o $$dir/alias.sv && \
	iverilog -g2012 -s pipeline_alias_tb -o $$dir/alias \
		rtl/pigen_primitives.sv $$dir/alias.sv tests/pipeline_alias_tb.sv && \
	vvp $$dir/alias; status=$$?; rm -rf $$dir; exit $$status

pipeline-syntax-test: pigen
	./tests/pipeline_syntax.sh ./pigen

biquad-bank-test: pigen
	./pigen examples/biquad_bank.pigen -o /tmp/pigen-biquad-bank.sv
	iverilog -g2012 -o /tmp/pigen-biquad-bank-vvp rtl/pigen_primitives.sv /tmp/pigen-biquad-bank.sv examples/biquad_bank_tb.sv
	vvp /tmp/pigen-biquad-bank-vvp

concat-misaligned-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/concat_misaligned.pigen -o $$dir/misaligned.sv && \
	iverilog -g2012 -s concat_misaligned_tb -o $$dir/misaligned \
		rtl/pigen_primitives.sv $$dir/misaligned.sv tests/concat_misaligned_tb.sv && \
	vvp $$dir/misaligned; status=$$?; rm -rf $$dir; exit $$status

concat-dest-test: pigen
	./pigen tests/concat_dest.pigen -o /tmp/pigen-concat-dest.sv
	iverilog -g2012 -o /tmp/pigen-concat-dest-vvp rtl/pigen_primitives.sv /tmp/pigen-concat-dest.sv tests/concat_dest_tb.sv
	vvp /tmp/pigen-concat-dest-vvp

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

compile-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-compile-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-compile-test tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c $(LDLIBS) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-compile-test || status=$$?; \
	rm -rf $$dir; exit $$status

clean:
	rm -f pigen
