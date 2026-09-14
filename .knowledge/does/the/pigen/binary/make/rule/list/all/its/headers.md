---
status: "unverified"
created_at: "2026-09-14T18:21:49+10:00"
scope: "local"
source: "Makefile pigen rule; observed make output 2026-09-14"
---

No: the pigen rule lists the direct .c files, src/fabric_svg.inc and a subset of include/pigen headers, but not ids.h (or source.h), which owners include transitively. On 2026-09-14, after editing include/pigen/ids.h, `make` reported "Nothing to be done for 'pigen'" and the binary was stale until a forced `rm pigen && make`, which then rebuilt cleanly with warnings as errors. Repairing the dependency list is left to a later task; until then, force-rebuild pigen after any header edit.
