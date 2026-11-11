CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS		= -lm
SEMANTIC_SOURCES = src/operation.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c
SYNTAX_SOURCES = src/syntax.c src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c

.PHONY: all clean test child-port-endpoint-test fabric-anonymous-test case-wildcard-test case-width-test case-signed-test case-nested-test ordinary-case-test case-guarded-complex-test ordinary-sv-preserve-test case-guarded-mixed-test mixed-write-conflict-error-test dependent-ordered-writes-test multiple-ordered-writes-test final-else-guard-dependency-test else-if-guard-dependency-test else-guard-dependency-test nested-guard-chain-dependency-test differing-guardless-dependency-test bare-uint-error-test int-ports-sv-test mixed-plain-always-test guardless-blocking-dependency-test same-guardless-dependency-test guardless-blocking-dependency-error-test dependent-parameter-override-test explicit-elastic-controls-test deep-guarded-blocking-dependency-test blocking-dependency-guard-error-test guarded-blocking-dependency-test blocking-dependency-inline-test directive-sv-test positional-elastic-child-test unpacked-struct-sv-test nba-dependency-split-test union-sv-test localparam-override-test generate-if-sv-test independent-blocking-function-test buffered-instance-local-conflict-error-test buffered-instance-producer-error-test mixed-clocked-task-error-test async-reset-always-sv-test async-reset-sv-test mixed-clocked-order-error-test nested-mixed-clocked-function-test mixed-clocked-function-test parent-parameter-override-test implicit-elastic-destinations-test implicit-elastic-child-test elastic-child-ports-test elastic-pigen-children-test parameterized-pigen-children-test enum-preserve-test interface-port-preserve-test concat-dest-test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test output-model-test fabric-test core-language-test pipeline-test pipeline-scope-test pipeline-syntax-test biquad-bank-test rtl-name-test rtl-lower-test route-test route-law-test route-vdo-test route-stall-test fabric-direct-duplicate-error-test clocked-case-error-test pipeline-reset-after-test pipeline-reset-conditional-test sv-integer-keywords-test integer-signal-test byte-type-test two-state-test validate-input-error-test peek-test validate-order-test macro-paste-decl-test macro-stringify-test macro-paste-ref-test macro-paste-test clog2-test pipeline-reset-test function-retain-test concat-placeholder-test struct-write-test pipeline-alias-test fabric-merge-test fabric-merge3-test concat-misaligned-test ternary-literal-test signed-cast-test typed-param-test integer-param-test pipeline-identity-test pipeline-output-test pipeline-stage-test pipeline-lower-test fsm-lower-test fsm-identity-test verify coslice-test coslice-typedef-test slicing-test signal-syntax-test validate-test signed-widen-test ready-break-test waveform compiler-waveform mac-waveform biquad-waveform text-waveform join-waveform fifo-waveform skid-waveform skid-compare-waveform port-waveform bram-waveform guarded-waveform output-waveform output-test clear-test fsm-test vertical-slice-test sv-emit-test compile-test coslice-xprop-test

all: pigen

pigen: src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c src/specialize.c
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c src/specialize.c $(LDLIBS)


test: pigen specialization-test child-port-endpoint-test fabric-anonymous-test case-wildcard-test case-width-test case-signed-test case-nested-test ordinary-case-test case-guarded-complex-test ordinary-sv-preserve-test case-guarded-mixed-test mixed-write-conflict-error-test dependent-ordered-writes-test multiple-ordered-writes-test final-else-guard-dependency-test else-if-guard-dependency-test else-guard-dependency-test nested-guard-chain-dependency-test differing-guardless-dependency-test bare-uint-error-test int-ports-sv-test mixed-plain-always-test guardless-blocking-dependency-test same-guardless-dependency-test guardless-blocking-dependency-error-test dependent-parameter-override-test explicit-elastic-controls-test deep-guarded-blocking-dependency-test blocking-dependency-guard-error-test guarded-blocking-dependency-test blocking-dependency-inline-test directive-sv-test positional-elastic-child-test unpacked-struct-sv-test nba-dependency-split-test union-sv-test localparam-override-test generate-if-sv-test independent-blocking-function-test buffered-instance-local-conflict-error-test buffered-instance-producer-error-test mixed-clocked-task-error-test async-reset-always-sv-test async-reset-sv-test mixed-clocked-order-error-test nested-mixed-clocked-function-test mixed-clocked-function-test parent-parameter-override-test implicit-elastic-destinations-test implicit-elastic-child-test elastic-child-ports-test elastic-pigen-children-test parameterized-pigen-children-test enum-preserve-test interface-port-preserve-test typed-param-test integer-param-test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test fabric-test core-language-test pipeline-test
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

case-guarded-mixed-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_guarded_mixed_error.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_guarded_mixed_tb -o $$dir/sim \
		tests/case_guarded_mixed_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS mixed case guards'; \
	status=$$?; rm -rf $$dir; exit $$status

clocked-case-error-test: pigen
	if ./pigen tests/clocked_case_error.pigen -o /tmp/case-error.sv 2>/tmp/case-error.err; then \
		echo 'structured case process unexpectedly compiled' >&2; exit 1; fi
	grep -q 'structured construct in an unsupported clocked process is not supported' /tmp/case-error.err

interface-port-preserve-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/interface_port_preserve.pigen -o $$dir/iface.sv && \
	test $$(grep -c '^interface bus_if;' $$dir/iface.sv) -eq 1 && \
	grep -Fq 'module m(input logic clk, bus_if bus' $$dir/iface.sv && \
	verilator --lint-only -Wall -Wno-fatal -Wno-DECLFILENAME \
		--top-module wrap $$dir/iface.sv; \
	status=$$?; rm -rf $$dir; exit $$status

enum-preserve-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/enum_preserve.pigen -o $$dir/enum.sv && \
	grep -Fq 'typedef enum logic [1:0]' $$dir/enum.sv && \
	iverilog -g2012 -s enum_preserve_tb -o $$dir/enum \
		tests/enum_preserve_tb.sv $$dir/enum.sv && \
	vvp $$dir/enum | grep -Fq 'PASS enum-typed variable preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

ordinary-sv-preserve-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/ordinary_sv_preserve.pigen -o $$dir/ordinary.sv && \
	grep -Fq 'generate' $$dir/ordinary.sv && \
	grep -Fq 'always_comb' $$dir/ordinary.sv && \
	grep -Fq 'typedef logic [3:0] nibble_t' $$dir/ordinary.sv && \
	grep -Fq 'localparam nibble_t MASK' $$dir/ordinary.sv && \
	iverilog -g2012 -s ordinary_sv_preserve_tb -o $$dir/ordinary \
		tests/ordinary_sv_preserve_tb.sv $$dir/ordinary.sv && \
	vvp $$dir/ordinary | grep -Fq 'PASS ordinary SV preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

directive-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/directive_sv.pigen -o $$dir/directive.sv && \
	test $$(grep -c '`default_nettype' $$dir/directive.sv) -eq 2 && \
	iverilog -g2012 -tnull $$dir/directive.sv; \
	status=$$?; rm -rf $$dir; exit $$status

bare-uint-error-test: pigen
	if ./pigen tests/bare_uint_error.pigen -o /tmp/bare-uint.sv \
		2>/tmp/bare-uint.err; then \
		echo 'bare uint unexpectedly compiled' >&2; exit 1; fi
	grep -q 'Pigen uint requires a width argument' /tmp/bare-uint.err

int-ports-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/int_ports_sv.pigen -o $$dir/int.sv && \
	grep -Fq 'input int a,' $$dir/int.sv && \
	grep -Fq 'output int y' $$dir/int.sv && \
	iverilog -g2012 -s int_ports_sv_tb -o $$dir/int \
		tests/int_ports_sv_tb.sv $$dir/int.sv && \
	vvp $$dir/int | grep -Fq 'PASS int ports preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

unpacked-struct-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/unpacked_struct_sv.pigen -o $$dir/struct.sv && \
	grep -Fq 'typedef struct {' $$dir/struct.sv && \
	verilator --lint-only -Wall -Wno-fatal -Wno-DECLFILENAME \
		--top-module structplain $$dir/struct.sv; \
	status=$$?; rm -rf $$dir; exit $$status

union-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/union_sv.pigen -o $$dir/union.sv && \
	grep -Fq 'typedef union packed' $$dir/union.sv && \
	iverilog -g2012 -s union_sv_tb -o $$dir/union \
		tests/union_sv_tb.sv $$dir/union.sv && \
	vvp $$dir/union | grep -Fq 'PASS packed union module preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

generate-if-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/generate_if_sv.pigen -o $$dir/genif.sv && \
	grep -Fq 'end else begin : h' $$dir/genif.sv && \
	iverilog -g2012 -s generate_if_sv_tb -o $$dir/genif \
		tests/generate_if_sv_tb.sv $$dir/genif.sv && \
	vvp $$dir/genif | grep -Fq 'PASS conditional generate preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

async-reset-always-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/async_reset_always_sv.pigen -o $$dir/async-always.sv && \
	grep -Fq 'always @(posedge clk or negedge reset)' $$dir/async-always.sv && \
	iverilog -g2012 -s async_reset_always_sv_tb -o $$dir/async-always \
		tests/async_reset_always_sv_tb.sv $$dir/async-always.sv && \
	vvp $$dir/async-always | grep -Fq 'PASS async-reset always block preserved'; \
	status=$$?; rm -rf $$dir; exit $$status

async-reset-sv-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/async_reset_sv.pigen -o $$dir/async.sv && \
	grep -Fq 'always_ff @(posedge clk or posedge reset)' $$dir/async.sv && \
	iverilog -g2012 -s async_reset_sv_tb -o $$dir/async \
		tests/async_reset_sv_tb.sv $$dir/async.sv && \
	vvp $$dir/async | grep -Fq 'PASS async-reset ordinary SV preserved'; \
	status=$$?; rm -rf $$dir; exit $$status
buffered-instance-local-conflict-error-test: pigen
	if ./pigen tests/buffered_instance_local_conflict_error.pigen \
		-o /tmp/buffered-local.sv 2>/tmp/buffered-local.err; then \
		echo 'instance/local buffered conflict unexpectedly compiled' >&2; exit 1; fi
	grep -q 'buffered destination has both instance and local producers' \
		/tmp/buffered-local.err

buffered-instance-producer-error-test: pigen
	if ./pigen tests/buffered_instance_producer_error.pigen -o /tmp/buffered-producer.sv \
		2>/tmp/buffered-producer.err; then \
		echo 'multiple buffered instance producers unexpectedly compiled' >&2; exit 1; fi
	grep -q 'buffered instance destination has multiple producers' \
		/tmp/buffered-producer.err

mixed-write-conflict-error-test: pigen
	if ./pigen tests/mixed_write_conflict_error.pigen -o /tmp/write-conflict.sv \
		2>/tmp/write-conflict.err; then \
		echo 'write/write conflict unexpectedly compiled' >&2; exit 1; fi
	grep -q 'a function statement and a Pigen statement write the same destination' \
		/tmp/write-conflict.err

mixed-clocked-task-error-test: pigen
	if ./pigen tests/mixed_clocked_task_error.pigen -o /tmp/mixed-task.sv \
		2>/tmp/mixed-task.err; then \
		echo 'task-call mixed clocked body unexpectedly compiled' >&2; exit 1; fi
	grep -q 'mixed clocked body with a task call cannot be split' \
		/tmp/mixed-task.err

mixed-clocked-order-error-test: pigen
	if ./pigen tests/mixed_clocked_order_error.pigen -o /tmp/mixed-order.sv \
		2>/tmp/mixed-order.err; then \
		echo 'unsafe blocking mixed clocked body unexpectedly compiled' >&2; exit 1; fi
	grep -q 'reads a value written by a blocking function statement' \
		/tmp/mixed-order.err
	if ./pigen tests/mixed_clocked_dependency_error.pigen -o /tmp/mixed-dep.sv \
		2>/tmp/mixed-dep.err; then \
		echo 'unsafe dependent mixed clocked body unexpectedly compiled' >&2; exit 1; fi
	grep -q 'reads a value written by a blocking function statement' /tmp/mixed-dep.err

dependent-ordered-writes-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/dependent_ordered_writes.pigen -o $$dir/dep.sv && \
	grep -Fq 'twice ( __pigen_expr_0 )' $$dir/dep.sv && \
	iverilog -g2012 -s dependent_ordered_writes_tb -o $$dir/dep \
		rtl/pigen_primitives.sv tests/dependent_ordered_writes_tb.sv \
		$$dir/dep.sv && \
	vvp $$dir/dep | grep -Fq 'PASS dependent ordered writes'; \
	status=$$?; rm -rf $$dir; exit $$status

multiple-ordered-writes-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/multiple_ordered_writes.pigen -o $$dir/multi.sv && \
	grep -Fq '__pigen_expr_1' $$dir/multi.sv && \
	iverilog -g2012 -s multiple_ordered_writes_tb -o $$dir/multi \
		rtl/pigen_primitives.sv tests/multiple_ordered_writes_tb.sv \
		$$dir/multi.sv && \
	vvp $$dir/multi | grep -Fq 'PASS multiple ordered writes'; \
	status=$$?; rm -rf $$dir; exit $$status

final-else-guard-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/final_else_guard_dependency.pigen -o $$dir/final.sv && \
	iverilog -g2012 -s final_else_guard_dependency_tb -o $$dir/final \
		rtl/pigen_primitives.sv tests/final_else_guard_dependency_tb.sv \
		$$dir/final.sv && \
	vvp $$dir/final | grep -Fq 'PASS final-else guard mux'; \
	status=$$?; rm -rf $$dir; exit $$status

else-if-guard-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/else_if_guard_dependency.pigen -o $$dir/elseif.sv && \
	iverilog -g2012 -s else_if_guard_dependency_tb -o $$dir/elseif \
		rtl/pigen_primitives.sv tests/else_if_guard_dependency_tb.sv \
		$$dir/elseif.sv && \
	vvp $$dir/elseif | grep -Fq 'PASS else-if guard mux'; \
	status=$$?; rm -rf $$dir; exit $$status

else-guard-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/else_guard_dependency.pigen -o $$dir/else.sv && \
	iverilog -g2012 -s else_guard_dependency_tb -o $$dir/else \
		rtl/pigen_primitives.sv tests/else_guard_dependency_tb.sv \
		$$dir/else.sv && \
	vvp $$dir/else | grep -Fq 'PASS else guard mux'; \
	status=$$?; rm -rf $$dir; exit $$status

nested-guard-chain-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/nested_guard_chain_dependency.pigen -o $$dir/chain.sv && \
	iverilog -g2012 -s nested_guard_chain_dependency_tb -o $$dir/chain \
		rtl/pigen_primitives.sv tests/nested_guard_chain_dependency_tb.sv \
		$$dir/chain.sv && \
	vvp $$dir/chain | grep -Fq 'PASS nested guard chain mux'; \
	status=$$?; rm -rf $$dir; exit $$status

deep-guarded-blocking-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/deep_guarded_blocking_dependency.pigen -o $$dir/deep.sv && \
	iverilog -g2012 -s deep_guarded_blocking_dependency_tb -o $$dir/deep \
		rtl/pigen_primitives.sv tests/deep_guarded_blocking_dependency_tb.sv \
		$$dir/deep.sv && \
	vvp $$dir/deep | grep -Fq 'PASS deep guarded blocking dependency'; \
	status=$$?; rm -rf $$dir; exit $$status

differing-guardless-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/differing_guardless_dependency.pigen -o $$dir/diff.sv && \
	iverilog -g2012 -s differing_guardless_dependency_tb -o $$dir/diff \
		rtl/pigen_primitives.sv tests/differing_guardless_dependency_tb.sv \
		$$dir/diff.sv && \
	vvp $$dir/diff | grep -Fq 'PASS differing guardless dependency'; \
	status=$$?; rm -rf $$dir; exit $$status

guardless-blocking-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/guardless_blocking_dependency.pigen -o $$dir/guardless.sv && \
	iverilog -g2012 -s guardless_blocking_dependency_tb -o $$dir/guardless \
		rtl/pigen_primitives.sv tests/guardless_blocking_dependency_tb.sv \
		$$dir/guardless.sv && \
	vvp $$dir/guardless | grep -Fq 'PASS guardless good'; \
	status=$$?; rm -rf $$dir; exit $$status

same-guardless-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/same_guardless_dependency.pigen -o $$dir/same.sv && \
	iverilog -g2012 -s same_guardless_dependency_tb -o $$dir/same \
		rtl/pigen_primitives.sv tests/same_guardless_dependency_tb.sv \
		$$dir/same.sv && \
	vvp $$dir/same | grep -Fq 'PASS guard same'; \
	status=$$?; rm -rf $$dir; exit $$status

guardless-blocking-dependency-error-test: pigen
	if ./pigen tests/guardless_blocking_dependency_error.pigen \
		-o /tmp/guardless-dep.sv 2>/tmp/guardless-dep.err; then \
		echo 'guardless blocking dependency unexpectedly compiled' >&2; exit 1; fi
	grep -q 'reads a value written by a blocking function statement' \
		/tmp/guardless-dep.err

blocking-dependency-guard-error-test: pigen
	if ./pigen tests/blocking_dependency_guard_error.pigen \
		-o /tmp/guard-dep.sv 2>/tmp/guard-dep.err; then \
		echo 'out-of-guard blocking dependency unexpectedly compiled' >&2; exit 1; fi
	grep -q 'reads a value written by a blocking function statement' \
		/tmp/guard-dep.err

guarded-blocking-dependency-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/guarded_blocking_dependency.pigen -o $$dir/guarded.sv && \
	iverilog -g2012 -s guarded_blocking_dependency_tb -o $$dir/guarded \
		rtl/pigen_primitives.sv tests/guarded_blocking_dependency_tb.sv \
		$$dir/guarded.sv && \
	vvp $$dir/guarded | grep -Fq 'PASS guarded blocking dependency'; \
	status=$$?; rm -rf $$dir; exit $$status

blocking-dependency-inline-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/blocking_dependency_inline.pigen -o $$dir/inline.sv && \
	grep -Fq 'assign __pigen_expr_0 = inc ( i );' $$dir/inline.sv && \
	iverilog -g2012 -s blocking_dependency_inline_tb -o $$dir/inline \
		rtl/pigen_primitives.sv tests/blocking_dependency_inline_tb.sv \
		$$dir/inline.sv && \
	vvp $$dir/inline | grep -Fq 'PASS blocking dependency inlined'; \
	status=$$?; rm -rf $$dir; exit $$status

nba-dependency-split-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/nba_dependency_split.pigen -o $$dir/nba.sv && \
	iverilog -g2012 -s nba_dependency_split_tb -o $$dir/nba \
		rtl/pigen_primitives.sv tests/nba_dependency_split_tb.sv \
		$$dir/nba.sv && \
	vvp $$dir/nba | grep -Fq 'PASS NBA dependency split'; \
	status=$$?; rm -rf $$dir; exit $$status

independent-blocking-function-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/independent_blocking_function.pigen -o $$dir/indep.sv && \
	iverilog -g2012 -s independent_blocking_function_tb -o $$dir/indep \
		rtl/pigen_primitives.sv tests/independent_blocking_function_tb.sv \
		$$dir/indep.sv && \
	vvp $$dir/indep | grep -Fq 'PASS independent blocking function split'; \
	status=$$?; rm -rf $$dir; exit $$status

mixed-plain-always-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/mixed_plain_always.pigen -o $$dir/plain.sv && \
	iverilog -g2012 -s mixed_plain_always_tb -o $$dir/plain \
		rtl/pigen_primitives.sv tests/mixed_plain_always_tb.sv \
		$$dir/plain.sv && \
	vvp $$dir/plain | grep -Fq 'PASS mixed plain always'; \
	status=$$?; rm -rf $$dir; exit $$status

nested-mixed-clocked-function-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/nested_mixed_clocked_function.pigen -o $$dir/nested.sv && \
	grep -Fq 'temp <= inc(i);' $$dir/nested.sv && \
	iverilog -g2012 -s nested_mixed_clocked_function_tb -o $$dir/nested \
		rtl/pigen_primitives.sv tests/nested_mixed_clocked_function_tb.sv \
		$$dir/nested.sv && \
	vvp $$dir/nested | grep -Fq 'PASS nested mixed clocked body'; \
	status=$$?; rm -rf $$dir; exit $$status

mixed-clocked-function-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/mixed_clocked_function.pigen -o $$dir/mixed.sv && \
	grep -Fq 'temp <= inc(i);' $$dir/mixed.sv && \
	iverilog -g2012 -s mixed_clocked_function_tb -o $$dir/mixed \
		rtl/pigen_primitives.sv tests/mixed_clocked_function_tb.sv \
		$$dir/mixed.sv && \
	vvp $$dir/mixed | grep -Fq 'PASS mixed clocked function and transfer'; \
	status=$$?; rm -rf $$dir; exit $$status

localparam-override-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/localparam_override.pigen -o $$dir/localparam.sv && \
	grep -Fq 'input  bit [15:0] i,' $$dir/localparam.sv && \
	iverilog -g2012 -s localparam_override_tb -o $$dir/localparam \
		rtl/pigen_primitives.sv tests/localparam_override_tb.sv \
		$$dir/localparam.sv && \
	vvp $$dir/localparam | grep -Fq 'PASS localparam override'; \
	status=$$?; rm -rf $$dir; exit $$status

dependent-parameter-override-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/dependent_parameter_override.pigen -o $$dir/dependent.sv && \
	grep -Fq 'output logic [31:0] tag,' $$dir/dependent.sv && \
	iverilog -g2012 -s dependent_parameter_override_tb -o $$dir/dependent \
		rtl/pigen_primitives.sv tests/dependent_parameter_override_tb.sv \
		$$dir/dependent.sv && \
	vvp $$dir/dependent | grep -Fq 'PASS dependent parameter override'; \
	status=$$?; rm -rf $$dir; exit $$status

parent-parameter-override-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/parent_parameter_override.pigen -o $$dir/parent-param.sv && \
	grep -Fq 'input  bit [15:0] i,' $$dir/parent-param.sv && \
	iverilog -g2012 -s parent_parameter_override_tb -o $$dir/parent-param \
		rtl/pigen_primitives.sv tests/parent_parameter_override_tb.sv \
		$$dir/parent-param.sv && \
	vvp $$dir/parent-param | grep -Fq 'PASS parent parameter override'; \
	status=$$?; rm -rf $$dir; exit $$status

implicit-elastic-destinations-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/implicit_elastic_destinations.pigen -o $$dir/implicit-dst.sv && \
	! grep -q 'pigen_buf #(.PAYLOAD_T(bit \[7:0\])) y8__pigen_buf' $$dir/implicit-dst.sv && \
	! grep -q 'pigen_buf #(.PAYLOAD_T(bit \[15:0\])) y16__pigen_buf' $$dir/implicit-dst.sv && \
	iverilog -g2012 -s implicit_elastic_destinations_tb -o $$dir/implicit-dst \
		rtl/pigen_primitives.sv tests/implicit_elastic_destinations_tb.sv \
		$$dir/implicit-dst.sv && \
	vvp $$dir/implicit-dst | grep -Fq 'PASS implicit elastic destinations'; \
	status=$$?; rm -rf $$dir; exit $$status

implicit-elastic-child-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/implicit_elastic_child.pigen -o $$dir/implicit.sv && \
	grep -Fq '.i_valid(a_valid), .i_ready(a_ready)' $$dir/implicit.sv && \
	! grep -Fq 'assign a_ready = 0;' $$dir/implicit.sv && \
	iverilog -g2012 -s implicit_elastic_child_tb -o $$dir/implicit \
		rtl/pigen_primitives.sv tests/implicit_elastic_child_tb.sv $$dir/implicit.sv && \
	vvp $$dir/implicit | grep -Fq 'PASS implicit parent input endpoint'; \
	status=$$?; rm -rf $$dir; exit $$status

positional-elastic-child-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/positional_elastic_child.pigen -o $$dir/pos.sv && \
	grep -Fq '.i_valid(a_valid), .i_ready(a_ready)' $$dir/pos.sv && \
	iverilog -g2012 -s positional_elastic_child_tb -o $$dir/pos \
		rtl/pigen_primitives.sv tests/positional_elastic_child_tb.sv \
		$$dir/pos.sv && \
	vvp $$dir/pos | grep -Fq 'PASS positional elastic child'; \
	status=$$?; rm -rf $$dir; exit $$status

explicit-elastic-controls-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/explicit_elastic_controls.pigen -o $$dir/explicit.sv && \
	test $$(grep -Fc '.i_valid(av)' $$dir/explicit.sv) -eq 1 && \
	iverilog -g2012 -s explicit_elastic_controls_tb -o $$dir/explicit \
		rtl/pigen_primitives.sv tests/explicit_elastic_controls_tb.sv \
		$$dir/explicit.sv && \
	vvp $$dir/explicit | grep -Fq 'PASS explicit elastic controls'; \
	status=$$?; rm -rf $$dir; exit $$status

elastic-child-ports-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/elastic_child_ports.pigen -o $$dir/elastic.sv && \
	grep -Fq '.i_valid(a_valid), .i_ready(a_ready)' $$dir/elastic.sv && \
	iverilog -g2012 -s elastic_child_ports_tb -o $$dir/elastic \
		rtl/pigen_primitives.sv tests/elastic_child_ports_tb.sv $$dir/elastic.sv && \
	vvp $$dir/elastic | grep -Fq 'PASS elastic child ports'; \
	status=$$?; rm -rf $$dir; exit $$status

elastic-pigen-children-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/elastic_pigen_children.pigen -o $$dir/elastic-param.sv && \
	test $$(grep -c '^module __pigen_spec_pchild_' $$dir/elastic-param.sv) -eq 2 && \
	grep -Fq '.i_valid(a_valid), .i_ready(a_ready)' $$dir/elastic-param.sv && \
	iverilog -g2012 -s elastic_pigen_children_tb -o $$dir/elastic-param \
		rtl/pigen_primitives.sv tests/elastic_pigen_children_tb.sv \
		$$dir/elastic-param.sv && \
	vvp $$dir/elastic-param | grep -Fq 'PASS elastic parameterized Pigen child ports'; \
	status=$$?; rm -rf $$dir; exit $$status

parameterized-pigen-children-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/parameterized_pigen_children.pigen -o $$dir/param-pigen.sv && \
	test $$(grep -c '^module __pigen_spec_pchild_' $$dir/param-pigen.sv) -eq 2 && \
	grep -Fq 'bit [7:0] b;' $$dir/param-pigen.sv && \
	grep -Fq 'bit [15:0] b;' $$dir/param-pigen.sv && \
	iverilog -g2012 -s parameterized_pigen_children_tb -o $$dir/param-pigen \
		rtl/pigen_primitives.sv tests/parameterized_pigen_children_tb.sv \
		$$dir/param-pigen.sv && \
	vvp $$dir/param-pigen | grep -Fq 'PASS parameterized Pigen children'; \
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
	./pigen examples/output_pipeline.pigen -o /tmp/pigen-output-test.sv
	verilator --binary --top-module output_pipeline_tb --Mdir /tmp/pigen-output-test-verilator rtl/pigen_primitives.sv /tmp/pigen-output-test.sv examples/output_pipeline_tb.sv
	/tmp/pigen-output-test-verilator/Voutput_pipeline_tb

output-waveform: pigen
	truncate -s 0 examples/output_pipeline.vcd
	./pigen examples/output_pipeline.pigen -o examples/output_pipeline.sv
	verilator --binary --trace --top-module output_pipeline_tb --Mdir /tmp/pigen-output-waveform-verilator rtl/pigen_primitives.sv examples/output_pipeline.sv examples/output_pipeline_tb.sv
	/tmp/pigen-output-waveform-verilator/Voutput_pipeline_tb

compile-test:
	@dir=$$(mktemp -d); \
	rm -f /tmp/pigen-compile-test; \
	status=0; \
	$(CC) $(CFLAGS) -o $$dir/pigen-compile-test tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c src/pipeline_identity.c src/pipeline_stage.c src/pipeline_lower.c src/fsm_identity.c src/fsm_lower.c src/fabric_lower.c $(LDLIBS) || { status=1; rm -rf $$dir; exit 1; }; \
	$$dir/pigen-compile-test || status=$$?; \
	rm -rf $$dir; exit $$status

clean:
	rm -f pigen

ordinary-case-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/ordinary_case.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s ordinary_case_tb -o $$dir/sim tests/ordinary_case_tb.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS ordinary case semantics'; \
	status=$$?; rm -rf $$dir; exit $$status

case-guarded-complex-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_guarded_complex.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_guarded_complex_tb -o $$dir/sim \
		tests/case_guarded_complex_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS complex case guards'; \
	status=$$?; rm -rf $$dir; exit $$status

case-width-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_width.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_width_tb -o $$dir/sim tests/case_width_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS common case width'; \
	status=$$?; rm -rf $$dir; exit $$status

case-signed-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_signed.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_signed_tb -o $$dir/sim tests/case_signed_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS common case signedness'; \
	status=$$?; rm -rf $$dir; exit $$status

case-nested-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_nested.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_nested_tb -o $$dir/sim tests/case_nested_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS nested case guards'; \
	status=$$?; rm -rf $$dir; exit $$status

case-wildcard-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/case_wildcard.pigen -o $$dir/case.sv && \
	iverilog -g2012 -s case_wildcard_tb -o $$dir/sim tests/case_wildcard_tb.sv rtl/pigen_primitives.sv $$dir/case.sv && \
	vvp $$dir/sim | grep -Fq 'PASS wildcard case guards'; \
	status=$$?; rm -rf $$dir; exit $$status

fabric-anonymous-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/fabric_anonymous.pigen -o $$dir/merge.sv && \
	iverilog -g2012 -s fabric_merge_tb -o $$dir/merge \
		rtl/pigen_primitives.sv $$dir/merge.sv tests/fabric_merge_tb.sv && \
	vvp $$dir/merge && \
	iverilog -g2012 -s fabric_merge_fair_tb -o $$dir/merge-fair \
		rtl/pigen_primitives.sv $$dir/merge.sv tests/fabric_merge_fair_tb.sv && \
	vvp $$dir/merge-fair; status=$$?; rm -rf $$dir; exit $$status

child-port-endpoint-test: pigen
	dir=$$(mktemp -d); \
	./pigen tests/child_port_unused.pigen -o $$dir/design.sv && \
	iverilog -g2012 -s child_port_unused_tb -o $$dir/sim tests/child_port_unused_tb.sv rtl/pigen_primitives.sv $$dir/design.sv && \
	vvp $$dir/sim | grep -Fq 'PASS unused child endpoints'; \
	status=$$?; rm -rf $$dir; exit $$status
	dir=$$(mktemp -d); status=0; \
	for fixture in expression expression_alias expression_join expression_conditional expression_concat expression_parameter_concat; do \
		tb=tests/child_port_expression_tb.sv; \
		case "$$fixture" in \
			expression_join|expression_conditional|expression_concat|expression_parameter_concat) tb=tests/child_port_$${fixture}_tb.sv ;; \
		esac; \
		./pigen tests/child_port_$$fixture.pigen -o $$dir/design.sv && \
		iverilog -g2012 -s child_port_endpoint_tb -o $$dir/sim $$tb rtl/pigen_primitives.sv $$dir/design.sv && \
		vvp $$dir/sim | grep -Fq 'PASS child port endpoint' || { status=1; break; }; \
	done; rm -rf $$dir; exit $$status
	dir=$$(mktemp -d); status=0; \
	for fixture in endpoint parent destination fabric forward module_order explicit_alias explicit_parent payload_alias valid_alias ready_alias payload_parent; do \
		./pigen tests/child_port_$$fixture.pigen -o $$dir/design.sv && \
		iverilog -g2012 -s child_port_endpoint_tb -o $$dir/sim tests/child_port_endpoint_tb.sv rtl/pigen_primitives.sv $$dir/design.sv && \
		vvp $$dir/sim | grep -Fq 'PASS child port endpoint' || { status=1; break; }; \
	done; \
	if [ $$status -eq 0 ]; then \
		for fixture in write missing consumers local_consumer explicit_consumers wire_view_consumers expression_consumers; do \
			if ./pigen tests/child_port_$${fixture}_error.pigen -o $$dir/error.sv 2>$$dir/error; then status=1; break; fi; \
			grep -Eq 'transfer destination is not a writable variable or signal|transfer value requires a compatible expression|elastic endpoint has multiple child consumers|elastic endpoint has both child and local consumers' $$dir/error || { status=1; break; }; \
		done; \
	fi; \
	if [ $$status -eq 0 ]; then \
		./pigen tests/child_port_surface.pigen -o $$dir/surface.sv && \
		iverilog -g2012 -s C -o $$dir/surface rtl/pigen_primitives.sv $$dir/surface.sv && \
		./pigen tests/child_port_nested.pigen -o $$dir/nested.sv && \
		iverilog -g2012 -s child_port_endpoint_tb -o $$dir/nested tests/child_port_nested_tb.sv rtl/pigen_primitives.sv $$dir/nested.sv && \
		vvp $$dir/nested | grep -Fq 'PASS child port endpoint' && \
		./pigen tests/child_port_introspection.pigen -o $$dir/introspection.sv && \
		iverilog -g2012 -s child_port_endpoint_tb -o $$dir/introspection tests/child_port_introspection_tb.sv rtl/pigen_primitives.sv $$dir/introspection.sv && \
		vvp $$dir/introspection | grep -Fq 'PASS child port endpoint' || status=1; \
	fi; rm -rf $$dir; exit $$status

.PHONY: specialization-test
specialization-test: pigen
	dir=$$(mktemp -d); \
	$(CC) $(CFLAGS) -o $$dir/check tests/specialization_test.c src/specialize.c src/lexer.c src/util.c $(LDLIBS) && \
	$$dir/check && \
	./pigen tests/deep_specialization.pigen -o $$dir/deep.sv && \
	iverilog -g2012 -s deep_specialization_tb -o $$dir/sim tests/deep_specialization_tb.sv rtl/pigen_primitives.sv $$dir/deep.sv && \
	vvp $$dir/sim | grep -Fq 'PASS deep specialization'; \
	status=$$?; rm -rf $$dir; exit $$status
