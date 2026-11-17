---
status: green
revised_at: "2026-11-17T11:53:44+11:00"
checked_at: "2026-10-04T16:19:56+11:00"
---

Pigen v1 extends ordinary SystemVerilog with ready/valid atomic transfers, independent data and transfer types, inline module-owned pipelines/fabrics and synchronous FSMs. It is source-to-source and does not change simulation timing.

Pigen showcases use int[n]/uint[n]/bit numerical types and always processes. logic serves compatibility; prefer always over always_ff. Transfers already enforce validity/readiness: avoid redundant valid guards and manual Verilog sign-extension scaffolding.

A runtime signal is data type × transfer type × declarator shape. Intended declarations are data-first, e.g. `int[16] fifo[8] pending[lanes];`. Pigen primitives are two-state int[n], uint[n] and bit/bit[n]; ordinary SV byte is its own signed type. Ordinary transfers include wire/reg/logic, buf/port/fifo/skid and abstract inputs; native AXI declarations are boundary-only module/fabric input/output ports, with child.port access for parent logic; contracts/channels/pins are implemented with acceptance in how/to/write/native/axi/declarations.md. Count brackets in declaration positions lower to descending ranges; explicit ranges and expression selects retain SV meaning. Shapes remain structural and indexing consumes unpacked dimensions before packed bits.

Ordinary transfer inputs have payload/valid inbound and ready outbound; native AXI channels follow their protocol roles. Unqualified inputs are abstract; explicit annotations constrain compatible peers. Data-type owner decides omission for internal/output signals. statics retain semantic identities even if control laws lower to constants.

`when (destination <= source)` performs a transfer and runs its body only on that cycle's fire. `if` tests computed conditions, including relational <=. Implementation status: how/to/write/fsm/states.md.

A transfer has one guard and fire event across all ready destinations and distinct valid consuming sources. Projections consume complete base tokens, repeated reads deduplicate, co-slices preserve packed bit-stream order, buffered destinations are whole, and mixed static writes share the fire. Ownership requires exclusive buffered producers/consumers; self-consumption and unbroken ready cycles are errors. peek observes payload without token dependencies. Local synchronous validity actions compose in source order and cannot modify upstream input validity.

Pigen actions bind to one synchronous clock edge; cross-domain/multi-edge/asynchronous-reset forms diagnose. Pipeline stages version a fixed buf-field namespace: reads use incoming state, writes define outgoing state, and initialized unwritten fields carry forward. Each boundary advances packets atomically; yield exposes the final result. Procedural guards gate stage handshakes; reset bindings clear validity. Fabrics connect native parent endpoints or child instance-port identities with per-connection payload compatibility, exclusive direct links or arbitrated routed links, blind interfaces, balanced three-port relative routing and registered-occupancy buffering. FSMs admit level-zero and state-gated code. Executed goto schedules a state and ends this FSM for the edge; earlier effects remain, next edge restarts at top. It never waits implicitly (how/to/write/fsm/states.md).

Child ports are parent-scope endpoints: `instance.port` is valid throughout the parent wherever its direction/type permit reads, writes or connections. Explicit wires remain supported. Partial positional connections (`A a(clk, reset);`) may leave data ports for endpoint use. Named connections accept sibling endpoints (`B b(.x_in(a.x_out));`). Anonymous `fabric begin b.x_out -> a.x_in; endfabric` is supported alongside named fabrics. Resolve instance/port identities, generate parent payload and applicable valid/ready nets, and apply type, direction and ownership checks.

Accepted non-Pigen SV must preserve widths, signedness, events, reset and observable cycles; accidental unsupported forms are not deliberate language restrictions. Diagnose meaning-preservation failures at original file/line/column. Textual identity is unnecessary. Typed record patterns follow how/to/construct/typed/records.md.

Related: [what is the systemverilog compatibility contract](systemverilog/compatibility/contract.md), [how does an atomic transfer fire](../../../how/does/an/atomic/transfer/fire.md).

The contextual unqualified input bit exception is documented in the input/omission owners; ordinary SV types retain their own static policy. Full remaining acceptance gates, deferred choices and the quarantined prototype language are separate owners. Token formatting is not syntax. Specified grammar and lowering requirements do not imply production support.

Build native AXI, UART/SPI/I2C/I2S and SDRAM cores with independent benches and compiler fixes. Internal views show actual source data paths, omit reset cases and inferred mux/decode blocks, and require visual checks of every view. Transforms preserve ready/valid, combiners AND valids, and statics are always ready/valid. Keep uniform clocks, stable controls, content-sized frames, opaque FIFO intrinsics, collapsed inspection and no call to action. Display details: how/are/pigen/transfer/types/displayed/in/test/artifacts.md. Coverage: what/is/the/standard/cores/library.md.

Native AXI4-Stream, AXI4-Lite and full AXI4 delivery prioritizes module inputs/outputs and fabrics. Protocol compatibility, parameterization and revisable syntax: what/are/the/native/axi/transfer/requirements.md. Broader SV work is separate unless it blocks those paths. Distinct memory-mapped fabric syntax remains open: what/is/the/proposed/native/axi/type/model.md.
