---
status: green
revised_at: "2026-11-14T16:46:50+11:00"
---

The pigen CLI compiles one .pigen input through specialization and the structured parse/semantic/RTL/output/emission path, with fabric diagram generation where present. Silent success writes the output, leaves stdout/stderr empty and exits 0. Compile failure writes a diagnostic to stderr, exits nonzero and preserves any existing output artifact.

Arguments: `pigen INPUT.pigen [-o OUTPUT.sv] [--diagram PATH | --no-diagram]`. --output is an alias for -o. Without an output option, a trailing .pigen becomes .sv; otherwise .sv is appended. Argument errors print usage and exit 2. Diagram options are accepted by this checkout's production CLI; they are not restricted to a separate prototype executable.

A nonempty valid diagnostic span prints path:line:column:message, the offending source line prefixed with four spaces, then a caret aligned to the reported column. Missing or zero-width spans take the one-line `pigen: message` fallback. EOF for an unfinished design unit exercises that fallback. Data-first declarations are accepted; transfer-first spellings such as `buf bit[8] stage` diagnose that the data type must precede the transfer type.

tests/cli_diagnostics_test.sh invokes the real driver in temporary directories, checking the exact spanned diagnostic, zero-width EOF fallback, silent success, no default output on failure and preservation of an explicit existing output. Its malformed inputs are created by the test rather than relying on formerly invalid repository fixtures that now compile. make cli-diagnostics-test runs it and verify includes that gate.
