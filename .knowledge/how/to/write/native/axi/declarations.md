---
status: green
revised_at: "2026-11-17T12:40:59+11:00"
---

Use data-first protocol qualifiers; optional storage precedes names. Without DATA: how/to/write/axi/streams/without/data.md.

```systemverilog
input uint[32] axi4_stream[keep=1,last=1,user=3] s;
output uint[32] axi4_lite[addr=40] m;
output uint[64] axi4[addr=40,id=4,awuser=3,wuser=7,buser=5,aruser=11,ruser=13] fifo[3] bus;
```

Native typedefs/bindings: how/are/native/axi/type/aliases/represented.md. Package contracts use `input native_buses::registers_t s;` or imported aliases through `import native_buses::registers_t;` / `import native_buses::*;` (how/are/native/axi/package/types/represented.md; how/are/native/axi/package/imports/resolved.md; examples/native_axi_packages.pigen).

input selects target/receiver; output selects initiator/transmitter. Memory-mapped endpoints have independent AW/W/B/AR/R channels, with B/R reversed. Internal AXI declarations reject; parent code uses child.bus channels and ordinary transfer records. Storage defaults to buf; buf/fifo/skid are accepted, while static and pulse storage reject. AXI declarations require module/fabric input/output ports, including aliases and arrays. Native endpoint arrays use ordinary declarator dimensions and constant lane coordinates; named/positional whole/selected-array bindings, indexed child projections and forward sibling arrays preserve independent lanes. Child collection ports accept `.s({s0,s1})` and mixed endpoint/collection items in declaration order (how/are/native/axi/collection/concatenations/resolved.md). Positional `.s('{s0,s1})`, constant index/type/default and nested row patterns provide target context (how/are/native/axi/collection/assignment/patterns/resolved.md). Row/range selections and positional rebasing: how/are/native/axi/collection/selections/resolved.md. Bounds, coordinate pin names and limits: how/are/native/axi/endpoint/arrays/represented.md. Generated processes and child endpoints are supported; remaining contexts: how/are/structured/generate/scopes/elaborated.md.

Named constant options: addr (memory-mapped, default 32), id (AXI4/Stream, default absent), dest/user (Stream), awuser/wuser/buser/aruser/ruser (full AXI4), and keep/strb/last (Stream booleans). Optional fields default absent. Duplicate, unknown, inapplicable or invalid options diagnose. DATA width comes from the preceding type. Each field obeys the generated-width limit; aggregate records may exceed it.

Select bus.aw/w/b/ar/r; Stream bus and bus.beat denote its beat channel. Fields include bus.aw.addr, bus.w.data/strb, bus.r.resp and stream.data/keep/last/user. DATA retains type/signedness; other fields are unsigned. Runtime payload projections: how/are/native/axi/field/projections/lowered.md. valid/ready/peek apply to channels. Write complete records; buffered field writes reject. Channel copies and ternary arms require compatible contracts and channel identities. Named patterns such as `m.r <= '{data:value,resp:status};` construct typed channels; how/to/construct/typed/records.md owns defaults, nesting and contextual casts. Equal payload widths alone do not permit an ordinary token to connect to AXI.

RTL emits standard flattened pins and independently handshaken channels. Native receivers capture external records into channel storage before consumption. READY follows registered occupancy; buf accepts only while empty. Use clock=ACLK with reset=RST or resetn=ARESETn for scalar static control inputs; defaults are clk/reset. All channels use that rising-edge domain and synchronous reset. Incompatible processes diagnose. Clock bindings: how/are/native/axi/clock/domains/bound.md.

Named `.bus(parent_bus)` and positional whole-bus child connections validate complete contracts, complementary views and parent clock/reset identities. Provisional `extern module` headers declare separately compiled SV IP contracts (how/are/native/axi/endpoints/mapped/to/external/systemverilog/ip.md; examples/native_axi_external.pigen). Child channels/fields use child.bus.aw and child.bus.aw.addr; forward sibling connections retain storage-free views. Native whole-interface fabrics are quarantined and rejected before semantic publication; their retained design is owned by how/are/native/axi/fabrics/connected/and/arbitrated.md.

Evidence: The retained pre-quarantine suite checks standard-pin transport, storage capacity, signed DATA, projections, records, hierarchy, independent stalls, queued reset and invalid connections. Native array tests score each lane independently. examples/native_axi_relay.pigen and examples/native_axi_generated.pigen supply parameterized relays; examples/native_axi_named_types.pigen demonstrates local aliases and address/ID overrides. Transport tests alone do not prove legal complete bursts or completion of the full AXI goal; the retained inactive composition witnesses have independent burst/backend checks.
