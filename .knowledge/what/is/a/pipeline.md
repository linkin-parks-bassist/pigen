---
status: green
revised_at: "2026-11-17T12:05:08+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A pipeline expresses elastic travelling-packet computation inside a clocked process using a fixed field namespace. The same x refers to a different stage register at each boundary. Reads see immutable incoming state; <= defines outgoing state for the next stage. Unwritten initialized fields travel forward with the packet, and related fields advance together under one handshake event.

Fields have buf transfer behavior; omission is canonical and explicit buf is equivalent. A final yield exposes the final stage result as a buffered source without adding another stage after that boundary. Several packets can occupy different stages concurrently. Guards and backpressure control advancement; reset clears generated validity.

Production versioning and packet transfer construction are implemented in src/resolve.c. how/do/pipeline/stage/reads/work.md owns the detailed laws and current independent simulation evidence. The separate structural pipeline service is not the production path.
