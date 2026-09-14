---
status: "unverified"
created_at: "2026-09-14T18:21:49+10:00"
scope: "local"
source: "David's current-only knowledge instruction; current Makefile and owner contracts, Codex /root 2026-09-14"
updated_at: "2026-09-14T20:49:23+10:00"
---

No. The Makefile pigen rule lists its direct C sources, src/fabric_svg.inc and a subset of public headers, but omits transitive headers including ids.h and source.h. A header-only edit can leave the production binary stale. Until the dependency rule is repaired, force a rebuild after header changes using make -B pigen. This does not establish a compiler logic defect; it is an unresolved build dependency issue.
