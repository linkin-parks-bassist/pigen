CC		= cc
CFLAGS		= -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
LDLIBS		= -lm

# Each C test program is built once per source change; every case runs that one binary.
TEST_BUILD	= build/tests
TEST_HEADERS	= $(wildcard include/pigen/*.h tests/*.h)
SEMANTIC_SOURCES = src/operation.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c
SYNTAX_SOURCES = src/syntax.c src/type_syntax.c src/expression.c src/preprocess.c src/lexer.c src/source.c src/util.c

.PHONY: all clean test source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test instance-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test output-model-test rtl-name-test rtl-lower-test route-test route-law-test route-vdo-test route-stall-test pipeline-identity-test pipeline-output-test pipeline-stage-test pipeline-lower-test fsm-lower-test fsm-identity-test instance-identity-test verify ready-break-test clear-test sv-emit-test compile-test

all: pigen

pigen: src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c
	$(CC) $(CFLAGS) -o $@ src/pigen.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS)

test: pigen source-test preprocess-test transfer-type-test syntax-model-test fsm-syntax-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test

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

verify: test ready-break-test clear-test

ready-break-test:
	iverilog -g2012 -s ready_break_tb -o /tmp/pigen-ready-break-vvp rtl/pigen_primitives.sv tests/ready_break_tb.sv
	vvp /tmp/pigen-ready-break-vvp

clear-test:
	verilator --binary --top-module fifo_discard_tb --Mdir /tmp/pigen-clear-verilator rtl/pigen_primitives.sv tests/fifo_discard_tb.sv
	/tmp/pigen-clear-verilator/Vfifo_discard_tb

$(TEST_BUILD)/pigen-compile-test: tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS) $(TEST_HEADERS) Makefile
	@mkdir -p $(TEST_BUILD)
	$(CC) $(CFLAGS) -o $@ tests/compile_test.c src/compile.c src/source.c src/util.c src/lexer.c src/preprocess.c src/syntax.c src/type_syntax.c src/expression.c src/transfer_type.c src/integer.c src/data_type.c src/semantic.c src/operation.c src/predicate.c src/resolve.c src/type_resolve.c src/expression_analysis.c src/expression_resolve.c src/expression_use.c src/rtl.c src/rtl_lower.c src/output.c src/sv_emit.c src/rtl_name.c src/blocks.c src/assignments.c src/declarations.c src/procedural.c src/transfer.c src/pipeline.c src/fsm.c $(LDLIBS)

compile-test: $(TEST_BUILD)/pigen-compile-test
	@rm -f /tmp/pigen-compile-test
	@$(TEST_BUILD)/pigen-compile-test

clean:
	rm -rf build
	rm -f pigen
