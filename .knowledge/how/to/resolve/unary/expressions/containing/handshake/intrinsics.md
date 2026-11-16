---
status: green
revised_at: "2026-11-15T05:48:09+11:00"
---

In src/expression_resolve.c, resolve_direct_expression handles syntax unary nodes containing ready/valid/accepts/peek by recursively resolving the operand, mapping the syntax unary operator, requesting the data-type owner's pigen_unary_resolution, applying any nonidentity operand conversion and constructing pigen_expr_add_unary. requires_direct_resolution selects trees containing handshake intrinsics or typed record-pattern casts. This keeps intrinsic expressions on the same typed operator/conversion contracts as ordinary analyzed expressions. Without that branch, `!ready(received)` fell through to ordinary analysis, which cannot resolve the intrinsic call, so a valid clocked guard was rejected. The I2S controller exercises negated ready and valid guards; a focused intrinsic-unary regression owns broader operator behavior.
