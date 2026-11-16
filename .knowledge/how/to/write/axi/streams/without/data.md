---
status: green
revised_at: "2026-11-16T14:11:10+11:00"
---

Use provisional void DATA syntax for AXI4-Stream beats without TDATA:

```systemverilog
typedef void axi4_stream[last=1,id=4,dest=3,user=7] metadata_t;
input metadata_t fifo[3] s;
output metadata_t skid m;
```

Select s.last/id/dest/user, or copy/route the complete beat. There is no .data field or TDATA pin. KEEP/STRB require DATA and reject here. Memory-mapped AXI rejects void. Counts and signedness cannot qualify the marker. Source aliases name the complete native contract; standalone `typedef void name_t;` and packed void record fields reject. The non-numerical marker has no RTL object type (how/are/void/data/types/represented.md).

`void axi4_stream` without sidebands gives handshake-only beats. Internal storage uses padding, while standard pins contain only TVALID/TREADY. Native aliases, buf/fifo/skid storage, child views, collections, keyed patterns, routed fabrics and extern-module contracts preserve protocol identity. Source syntax remains revisable. examples/native_axis_metadata.pigen demonstrates both forms and passes fatal lint for both tops (/tmp/pigen-axis-metadata-example.sv).

Independent verification comprises 52 positive profiles and eight output-preserving rejection cases:

- tests/native_protocol_test.py scores 21 metadata profiles across all storage kinds, direct/child/internal routes, aliases and separate SV IP; it checks exact capacity, metadata values, stalls/reset and omitted pins. Six additional handshake-only profiles check 257 tokens with queued reset.
- tests/native_array_test.py checks nine four-lane binding, child/fabric, selection and type-key profiles.
- tests/native_axis_fabric_test.py checks nine three-source arbitration profiles with and without LAST, through child/boundary/standalone/internal/collection paths. USER carries an independent source/sequence witness; packets, all sidebands, fairness and stalls are checked without TDATA.
- tests/native_axis_routing_test.py checks seven TDEST switch profiles, including generated arrays, colliding/absent IDs and seven-source multi-hop tag provenance. USER also carries a reset-epoch witness.

All pass with compiler ASan/UBSan, leak detection and halt-on-error (/tmp/pigen-void-final-asan.log; /tmp/pigen-void-final-asan-aoiduijb). Final full repository verification passes (/tmp/pigen-void-complete-verify.log).

Reset follows the declared synchronous domain. The token witness samples cleared output VALID after the active reset edge; target READY need not be low during reset. Protocol model details: how/are/native/axi/protocol/contracts/represented.md.
