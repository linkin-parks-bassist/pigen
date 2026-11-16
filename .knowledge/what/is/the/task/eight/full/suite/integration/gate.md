---
status: green
revised_at: "2026-11-15T13:13:35+11:00"
---

The production integration gate is make verify, after building pigen with bounded parallelism (how/to/run/repository/verification.md). Makefile targets are the executable suite definition in this checkout. rtl-lower-test, rtl-test, rtl-name-test, transfer-type-test and resolve-test exercise the transfer-owner layers; independent emitted RTL simulations exercise actual data, handshakes, stalls and reset.

Task 8's structural sections in tests/rtl_lower_test.c share one file-local rtest_collect_exprs/rtest_shares_nonconst pair. The accessor, module, fire-identity, join, guard, ownership, projection, static and process-order sections compile together without duplicate helper definitions. Projection/static/process-order witnesses do not use those expression-sharing helpers.

A green integration run establishes the exercised checks, not stronger guarantees stated by legacy comments. The current t8-guard sharing/equality witness is not independent because it copies the update expression into its purported ready-expression variable. what/is/elastic/rtl/task/eight.md owns that limitation and the next meaningful check against actual update enables and source-ready equations. Payload publication uses RHS data and explicit enables; update values must not be described as replacement fire bits. Source-reference identity, repeated module composition and cleanup are checked by tests/generated_instance_test.c (how/are/source/paths/mapped/to/rtl/names.md).

The remaining frontier belongs in what/is/the/plan.md. Historical hidden-packet merges and master-branch manifest states are not current completion evidence for this fork.
