---
status: green
revised_at: "2026-09-14T20:36:21+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Ready-dependency validation. Create ready_graph.h/ready_graph.c/ready_graph_test.c. Build identity graph from deduplicated incidence and downstream-ready realization laws; constants/external/occupancy terminate propagation. Deterministic Tarjan rejects SCC cycles/self-edge, diagnoses source-ordered closing transfer. Gate: chain/cycle/self-loop/FIFO-broken cycle, declaration permutations and duplicate projection incidence; make ready-graph-test resolve-test. This is approved future work, not implemented status.

pigen_validate_ready_dependencies(model, error) reports exactly combinational ready-dependency cycle for the planned cycle case. Test a->b->c, a->b->c->a, a->a and a FIFO-broken cycle. Build one vertex per downstream-ready realization and terminate constant/external/occupancy dependencies. Tarjan rejects multi-vertex SCCs or self edges, selecting the lowest source-ordered closing transfer. Declaration insertion permutations must not alter diagnostic choice when transfer order is fixed; repeated projections do not create duplicate vertices/false cycles. Free all temporary arrays on every exit. Files ready_graph.h/c, ready_graph_test.c, Makefile.
