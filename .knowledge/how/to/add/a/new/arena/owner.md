---
status: "unverified"
created_at: "2026-09-14T18:02:03+10:00"
scope: "local"
source: "Codex /root document-retirement live-link check and exact owner paths 2026-09-14"
updated_at: "2026-09-14T20:42:40+10:00"
---

A pigen structured owner keeps one pointer/count/capacity triple per record kind in its model struct (e.g. pigen_rtl_model.types/type_count/type_capacity). Records are plain data: provenance is a pigen_source_span, never a source-text pointer, and enum fields keep zero invalid. Add functions guard count == PIGEN_INVALID_ID with pigen_fail, grow the arena to double its capacity (first size 8) via pigen_resize, return the id with index equal to the count before increment, then fill the record. Get functions take a const model, return NULL for a null model, PIGEN_INVALID_ID, or an index at/after count, and otherwise return a pointer into the arena; callers must re-acquire borrowed pointers after any growth. The owner free function releases every arena and zeroes the whole struct with *model = (type){0}. Tests live in tests/<name>_test.c as an assert-based standalone main ending in a PASS line; the Makefile target compiles the test with only the owner's own sources under $(CC) $(CFLAGS) into /tmp, runs the binary, and the target joins .PHONY and the test list. The failing test is added before the implementation.

Related: [how are typed identities implemented](../../../../../are/typed/identities/implemented.md), [how should compiler builders fail](../../../../../should/compiler/builders/fail.md), [does source identity survive arena growth](../../../../../../does/source/identity/survive/arena/growth.md), [how to test the structured foundation](../../../../test/the/structured/foundation.md).
