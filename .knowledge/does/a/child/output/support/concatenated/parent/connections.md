---
status: green
revised_at: "2026-11-15T10:33:23+11:00"
---

Yes. src/resolve.c:resolve_instance validates an ordinary output connection through pigen_lvalue_is_writable, which recursively admits concatenations of writable parent projections and rejects read-only input destinations.

The child-output-concat case in tests/cores/run.py compiles tests/cores/child_concat.pigen, lints it and simulates 200 buffered byte transfers into two four-bit parent outputs with backpressure. Negative witnesses reject a concatenation containing a parent input and overlapping producers targeting the same concatenation. Computed input and explicit-valid semantics remain governed by how/to/connect/child/ports.md. This evidence does not establish arbitrary multidimensional or dynamic concatenated token mappings.

Native AXI collection bindings use a separate endpoint-identity path in pigen_resolve_protocol_array_binding. They assemble whole independently handshaken endpoints rather than splitting a buffered payload into pieces. tests/native_array_test.py covers scalar, mixed slice, row and child-collection concatenations across all three AXI families. The Stream core cores/axis/fifo.pigen declares native protocol ports.
