---
status: green
revised_at: "2026-11-02T21:20:04+11:00"
---

No. The Makefile pigen rule (Makefile lines 15-16) lists only its 36 direct C sources as prerequisites and names no headers at all; src/fabric_svg.inc enters the build only through `#include "fabric_svg.inc"` in src/blocks.c:623 and is likewise not a prerequisite. Transitive headers including ids.h and source.h are therefore omitted: a header-only edit can leave the production binary stale. Until the dependency rule is repaired, force a rebuild after header changes using make -B pigen. This does not establish a compiler logic defect; it is an unresolved build dependency issue.
