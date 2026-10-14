---
status: green
revised_at: "2026-10-14T16:41:24+11:00"
---

## Quarantined Compiler Composition Interface

The composition interface isolates the compiler frontend from the prototype monolith. It provides a clean, testable API for compilation with proper error handling and resource management.

### Public API

```c
// compile.h

typedef struct {
    size_t maximum_generated_bits;
} pigen_compile_options;

typedef struct {
    char *systemverilog;
    size_t length;
} pigen_compile_result;

typedef struct {
    pigen_origin_id origin;
    pigen_source_span span;
    const char *message;
} pigen_compile_error;

// Compiles Pigen source to SystemVerilog.
// sources: source manager owning all input files (immutable after add)
// source: the primary source path and content
// options: compilation options (e.g., maximum_generated_bits)
// result: output buffer containing generated SV on success
// error: populated on failure with native origin/span information
int pigen_compile_source(pigen_source_manager *sources,
    const char *source_path, const char *source_text, size_t source_length,
    const pigen_compile_options *options,
    pigen_compile_result *result, pigen_compile_error *error);

// Frees the compile result. Does not free sources or error.
void pigen_free_compile_result(pigen_compile_result *result);
```

### Ownership Contract

- **sources**: Owned by the caller. The compiler borrows references but does not free them.
- **result->systemverilog**: Owned by the compiler until `pigen_free_compile_result` is called.
- **error**: Out-parameter populated on failure. The `message` pointer is borrowed from internal state; caller must copy if needed beyond the next compile call.
- **Cleanup**: On failure, `result` is untouched (NULL/0). On success, `error` is cleared.

### Error/Source-Span Contract

- Native origin identities are preserved through all phase errors.
- Source spans are the strongest available (file + byte offset range).
- Error adaptation must not discard macro-origin or source-location information.
- Each phase (lexing, preprocessing, lowering, emitting) can produce errors with native spans.

### Quarantine Requirements

- No search for `composer`, `lowering`, or `emitter` names in `src/pigen.c`, `blocks.c`, `pipeline.c`, `fsm.c`.
- The compile interface is the only bridge between the prototype and external consumers.
