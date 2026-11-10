---
status: green
revised_at: "2026-11-10T16:34:48+11:00"
---

pigen-fork runs a structured-only compiler. `make test` and `make verify` are fully green; bounded parallel full verification (`make -j8 pigen` then `make -j8 --output-sync=target verify`) completed in 21.85 s wall on the 32-logical-CPU host with no failures. Done and verified: declarations, parameter-dependent widths, abstract inputs, two-state Pigen integers, distinct SystemVerilog integer keyword types, static/buffered transfers with primitives, clocked processes, pipelines, FSMs, transfer-and-branch, accessors, source-ordered validity actions, peek, concatenation/misaligned destinations, `_` placeholders, packed structs, retained functions/tasks, macros, inline and standalone fabric routing with fairness/path metadata/SVG, typed/integer parameters, `$clog2`, signedness reinterpretation, indexed part-selects, ordinary parameterized child modules and instances, generate/initial/always_comb retention, typedef and parameter emission, interface/directionless-port and enum modules emitted verbatim, pure opaque designs, parameterized Pigen child specialization, and elastic child-port payload/valid/ready connection expansion for ordinary and parameterized children when the parent supplies explicit control nets.

Remaining work:
1. Resolve elastic child ports against implicit parent endpoint siblings. Explicit parent `a_valid`/`a_ready` nets now work, but a parent buffered signal’s compiler-generated valid/ready ports cannot yet be named from an instance connection.
2. Continue the requirement-by-requirement acceptance audit against `what/is/the/spec.md`, including the compatibility contract; do not shrink requirements to currently supported syntax. Fixed findings so far include source-order validity composition, unused-input ready defaults, peek dependencies, input-validity actions, two-state Pigen integer emission, integer keyword types, guard-bound pipeline resets, mixed clocked diagnostics, fabric direct exclusivity, parameter-dependent widths, ordinary parameterized children, ordinary SV item/typedef/parameter/interface/enum preservation, pure opaque output, parameterized Pigen child specialization, and explicit-control elastic child ports. Add failing witnesses before implementing further gaps.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
