---
status: green
revised_at: "2026-11-15T13:13:35+11:00"
---

Buffered scalar/channel ownership is a resolver obligation (why/are/buffered/consumers/exclusive.md). A consuming buffered endpoint has one logical consumer and a produced buffered endpoint has one logical producer; repeated syntactic transfers may share an endpoint when their guard predicates are mutually exclusive. Non-consuming observations remain separate. Native whole-bus fabrics and child views retain their additional canonical ownership rules (how/are/native/axi/fabrics/connected/and/arbitrated.md).

src/resolve.c:validate_signal_ownership runs during semantic resolution. Overlapping non-exclusive CONSUMER or PRODUCER uses of a requires_ownership signal reject with signal has nonexclusive consumers/producers. Descriptor consumes_on_read and produces_on_write determine the roles. pigen_predicates_mutually_exclusive establishes permitted guarded sharing. Static declarations keep their separate source-ordered write laws.

Transfer lowering consumes those validated identities and does not reimplement the ownership solver. RTL updates retain payload data and explicit fire enables; buffered writes use their storage-input drives. Historical claims that destination update values are fire identities are not the production data contract (what/is/the/task/eight/lowering/destination/publication.md).

tests/resolve_test.c and the t8-ownership section of tests/rtl_lower_test.c exercise invalid competing buffered reads/writes and mutually exclusive sharing. Lowering checks cover owner-validated process/update structure. Production acceptance also uses the independent RTL gates in make verify. A passing structural test does not establish untested aliases or guard equivalence; what/is/elastic/rtl/task/eight.md owns the current sharing-probe limitation.
