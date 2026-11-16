---
status: green
revised_at: "2026-11-15T23:23:16+11:00"
---

`$bits` and fixed-array inquiries inspect their first operand's type without evaluating its data (how/are/fixed/array/inquiries/resolved.md). Constant dimensions materialize constants; runtime dimensions retain ordinary evaluated reads.

PIGEN_ANALYZED_CASE_VALUE in expression_analysis.h wraps a dimension through as.group.operand, retaining its type/shape and an invalid constant recipe. materialize copies its context with case_mode enabled and applies selection_fill_context for filled arguments, restoring aliases/dimensions. This prevents the enclosing query's 32-bit result from resizing the dimension and preserves local fill semantics without publishing during analysis. It creates no new semantic operator. Pure constant-context evaluation refuses this runtime form.

Analyzed handshake CALL records preserve these boundaries inside inquiries and runtime dimension expressions (how/are/handshake/calls/analyzed.md).

Supported $signed, $unsigned, peek and filled constant $clog2 arguments have local sizing boundaries in src/expression_resolve.c. resolve_call_argument detects fills recursively, resolves that argument in the CASE literal domain and uses selection_fill_context to apply its self-determined width while restoring the original type, packed dimensions and aliases. Arguments without fills retain their ordinary intrinsic route. Enclosing assignment width does not resize the argument before the call.

$signed/$unsigned preserve argument width and two-state/four-state domain while choosing the requested signedness. The shared explicit-conversion owner supplies the conversion. A fresh group represents an identity conversion; the result carries is_context_cast so later comparison/assignment context preserves its boundary without mutating the source expression. Thus $signed('1) is signed one-bit -1 and sign-extends when assigned wider, while $unsigned('1) zero-extends the one-bit value 1.

peek receives the locally normalized argument, then publishes an ordinary typed PEEK expression. It adds no storage or token consumption. Other consuming references still establish normal atomic ownership; observing another field cannot create an independent bus. Handshake semantics: does/a/continuous/assignment/lower/handshake/intrinsics.md.

A filled $clog2 argument must evaluate to a nonnegative host-unsigned value through the existing constant evaluator. Zero returns zero; other values return the ceiling of base-two logarithm, using the normal unsized integer result type. Runtime, unknown and unrepresentable values reject with a constant-argument diagnostic. Nonfilled $clog2 retains its analyzed constant path. Constant-only CASE analysis also normalizes filled arguments and represents $signed/$unsigned as typed CAST nodes without semantic expression publication (how/are/analyzed/constants/contextualized.md). This does not add general runtime $clog2 or arbitrary function/task calls.

contains_assignment_fill visits call arguments; ordinary filled calls select direct resolution, while inquiry analysis owns its separate argument boundary. Malformed/unsupported calls still reject. resolve_runtime supplies a source-located generic call diagnostic when fallback analysis fails without its own message. Failed construction restores expression/child/constraint snapshots. valid/ready/accepts retain their existing endpoint/control requirements and do not accept arbitrary values merely because they contain fills.

Evidence: tests/expression_resolve_test.c checks call values, nested calls, two-state signedness results, invalid arities, unsupported calls and constant-only failures with rollback. tests/cores/continuous_intrinsics* compare twenty call results with native SV over 1024 inputs and X/Z conditions, with fatal lint for called_fills. tests/native_protocol_test.py scores called_fill normal/when/FSM and mixed consuming/peek forms for Stream, Lite and full AXI under stalls and reset. Fill discovery/context: how/are/assignment/fill/literals/resolved.md.
