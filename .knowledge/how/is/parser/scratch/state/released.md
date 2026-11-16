---
status: green
revised_at: "2026-11-15T20:50:07+11:00"
---

src/syntax.c:free_parser_state releases parse-lifetime lookup vectors: typedef_name_facts, parameterized_module_names, pending_parameterized_instances and function_names. The syntax tree separately owns its node/type/expression/shape arenas; those are released by pigen_free_syntax_tree. Expanded source is borrowed.

pigen_parse_syntax zero-initializes syntax_parser and calls free_parser_state on success and on post-allocation failure exits. Function-name discovery allocates its own vector through remember_function_name; retaining a function and later rejecting a structured clocked call must still release that vector. Grammar diagnostics and resource cleanup have separate obligations: preserving a committed error cannot bypass scratch cleanup.

Evidence: src/syntax.c owns these allocations and terminal paths. tests/pipeline_function_mix_error.pigen exercises retained function discovery followed by a committed clocked-process error; tests/pipeline_syntax.sh checks the diagnostic. ASan/LeakSanitizer compiler checks use that failure path alongside successful native foreach example emission. The public tree vocabulary is owned by what/is/the/current/syntax/node/vocabulary.md; parser recognition is in how/are/structured/generate/constructs/parsed.md.
