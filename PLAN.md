# Pigen v1 implementation plan

## Contract

Pigen is a C17 source-to-source elaborator for synchronous ready/valid
datapaths.  It preserves ordinary SystemVerilog outside Pigen-owned syntax and
lowers transport storage through the explicit modules in
`rtl/pigen_primitives.sv`.

The v1 surface is the SystemVerilog-shaped language defined in `SPEC.md`:
packed-range transport declarations, ANSI transport ports, synchronous `always_ff`
domains, parsed structured controls, and state-block FSMs.  `accepts(y, x)` is
the handshake predicate; `invalidate(x)` drops one offered buffered item and
`flush(x)` clears a buffered value.  Buffered fan-out and potentially
overlapping transfer/clear operations are compile-time errors.

## Delivery sequence

1. Add source-span tokens and diagnostics.
2. Parse Pigen declarations, module headers, procedural controls, transport
   actions, clears, and FSMs into AST nodes while preserving unrelated SV.
3. Bind names, domains, transport operands, ownership, control exclusivity,
   and reset semantics in semantic analysis.
4. Lower semantic nodes into a dedicated RTL IR: primitive instances, port
   adapters, control enables, routes, clear requests, register updates, and
   FSM state logic.
5. Make SystemVerilog emission formatting-only and remove statement-oriented
   discovery.
6. Cover every language rule with parser/semantic regressions and every
   storage or transfer change with Verilator lint/simulation.  `make verify`
   is the milestone gate.

## Architecture

The frontend is divided into lexer, parser, semantic analysis, lowering, and
emission.  `pigen.c` remains CLI/pass coordination only.  Existing utility,
declaration, assignment, and procedural code is migrated into those layers;
new language features must not be implemented as textual substitutions.
