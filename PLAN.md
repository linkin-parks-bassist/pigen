# Pigen implementation plan

This is a local working plan and is intentionally excluded from commits.

## Development policy

Pigen is pre-release. New decisions may replace earlier Pigen extension syntax
and generated interfaces. Ordinary SystemVerilog compatibility is a permanent
constraint: accepted SV must retain its observable behavior, and any deliberate
restriction must be specified and diagnosed rather than emerging accidentally
from the parser or lowering.

## Overarching compatibility work

- [ ] Build a representative pure-SystemVerilog pass-through corpus covering
  modules, interfaces, packages, classes, generate blocks, procedural forms,
  assignment sizing/signedness, timing controls, assertions, and preprocessor
  use; compare simulations before and after Pigen.
- [ ] Add mixed-source regressions proving that introducing one Pigen construct
  does not reinterpret unrelated ordinary SV in the same file or design.
- [ ] Audit every current ordinary-SV rejection and classify it as a specified
  deliberate restriction or an implementation bug. Remove or fix accidental
  restrictions.
- [ ] Treat silent behavior changes in ordinary SV as release-blocking bugs.

## Locked fabric endpoint model

- A module input transport is source-blind.
- A module output transport is destination-blind.
- A fabric connection is `instance.output_port -> instance.input_port`; neither
  side has a third source, destination, origin, route, or address component.
- Each output port appears in exactly one connection and reaches exactly one
  input port.
- Several routed output ports may target one input port; the fabric arbitrates
  complete ready/valid transfers and does not expose the winning source to the
  receiver.
- One-to-many behavior uses an explicit splitter with one input and several
  ordinary single-destination outputs. Payload-address interpretation belongs
  in that splitter or a future protocol-specific fabric, not in endpoint ports.
- Endpoint transport kind is local. Fabric lowering observes payload and the
  ready/valid contract; `wire` and `logic`/`reg` use their emitted degenerate
  handshake constants.

## Completed baseline

- [x] Parse and lower SystemVerilog-shaped modules with transport
  declarations, actions, atomic joins, buffered storage, ports, and FSMs.
- [x] Parse top-level `pipeline` units in the compiler and emit elastic stage,
  skid, and public wrapper modules into the normal SystemVerilog output.
- [x] Support typed/inherited packed tuples, width and arity checks, stage-local
  combinational logic, parameters, periodic skids, and per-stage overrides.
- [x] Parse top-level `fabric` units in the compiler and emit direct links,
  endpoint queues, blind routers, routes, and the public wrapper module into
  the normal SystemVerilog output.
- [x] Support flattened ready/valid endpoints, deterministic balanced topology,
  route verification, round-robin router arbitration, and embedded
  route-manifest comments.
- [x] Render deterministic fabric SVGs from the integrated compiler topology,
  faithfully preserving the original tree-seeded spring relaxation, angular
  forces, footprint collision resolution, disconnected-component packing,
  crossing minimization, peer-directed ports, label adjustment, SVG structure,
  and connection legend; support default, custom, suppressed, and multi-fabric
  output paths without a Python runtime.
- [x] Parse ordinary modules, pipelines, and fabrics at top level while
  preserving `pipeline` and `fabric` identifiers inside ordinary units.
- [x] Cover positive behavior, randomized stalls, arbitration, full-throughput
  queues, mixed-unit files, direct-only fabrics, and negative diagnostics in
  the test suite.
- [x] Define degenerate validity actions: `validate` on `wire`, `reg`, or
  `logic` warns and is a no-op; `invalidate` is rejected.
- [x] Reject a consuming transfer that feeds a buffered destination from
  itself, including self-consumption across members of a co-sliced group.
- [x] Make every FIFO and skid an unconditional combinational ready-chain
  break: input readiness derives only from registered occupancy. Cover the
  contract by toggling downstream readiness while each primitive is full.
- [x] Treat `x <= {a, b}`, `{x, y} <= source`, and
  `{x, y} <= {a, b}` as the same atomic flattened bit-stream transfer. Allow
  different RHS/LHS arities and individual widths, and repartition in ordinary
  SystemVerilog concatenation order.
- [x] Treat every unpeeked payload projection as a read of its complete base
  transport; deduplicate repeated projections within one transfer group and
  retain mutually-exclusive branch consumers.
- [x] Guard concatenated and projected transfers with aggregate `$bits` checks
  elaborated by the downstream SystemVerilog tool, so typedefs, parameters,
  variable part-selects, and nested lvalues use native SV width rules.
- [ ] Move aggregate mismatch diagnostics from generated elaboration checks to
  original Pigen source locations when typed expression information permits.
- [ ] Replace textual transport occurrence scanning with parsed expression
  nodes carrying base transport, projection, use context, and conditional
  evaluation predicate.
- [x] Define degenerate slicing behavior: `reg`/`logic` and ordinary lvalues are
  always-ready atomic destination members and may use normal SV projections;
  pure degenerate statements retain ordinary NBA ordering; buffered ownership
  rules do not apply to them. A procedural `wire` destination is diagnosed.
- [ ] Implement `transfer begin ... end` as readable atomic-transfer syntax,
  exactly equivalent to concatenating member LHS and RHS expressions in source
  order. Include transport reads in lvalue indices/selects in the deduplicated
  validity and single-consumer analysis.
- [ ] After inline pipelines are stable, design stage-body transport operations
  whose pending handshake stalls that stage, plus an explicit `stall();`
  directive. Do not add an implicit pipeline `busy` mechanism; independent
  packets must remain dispatchable every cycle when ready/valid permits.
- [ ] Add contextual expression typing for packed transport and inline-pipeline
  outputs. A yielded expression should inherit the uniquely determined
  destination bit range's width and signedness, including across a stage
  repartition; arithmetic should then extend, truncate, and sign-propagate as
  that destination requires. This needs parsed typed expression nodes rather
  than the current token-level lowering, and should eliminate source-level
  width casts such as `acc_t'(a1)` in ordinary pipeline code.
- [x] Validate the fixed-point arithmetic model in `examples/biquad_bank.pigen`
  against a reference model. The regression loads eight independent coefficient
  sets, captures external per-filter traces, proves an initial full-rate burst,
  then spaces revisits beyond writeback latency for stateful reference checks.
- [ ] Build whole-unit ready-dependency graphs and reject indirect
  combinational cycles that do not cross a FIFO, skid, or ordinary register.

## Next milestones

1. Apply the blind-endpoint clean break throughout fabric parsing, checking,
   lowering, examples, and tests: use two-component endpoints, bind every
   output exactly once, remove public route/path/origin metadata, and retain
   internal fixed routes plus many-to-one arbitration.
2. Define declarations that bind pipeline and fabric endpoints directly to
   named module ports.
3. Generalize fabric payload typing beyond one fabric-wide `PAYLOAD_W`, with
   compile-time compatibility diagnostics at every connection.
4. Define and test an explicit payload-directed splitter component using one
   input transport and several separately connected output transports.
5. Make arrow tiers and `objective` drive deterministic topology optimization;
   retain the current balanced topology as the stable baseline and fallback.
6. Generalize router and endpoint depths after defining their throughput,
   latency, and generated-interface guarantees.
7. Replace remaining text-oriented block lowering with shared compiler model
   nodes where that improves source diagnostics and cross-unit validation.
8. Add cross-unit name, parameter, and interface validation before emission.
9. Extend randomized verification to larger fabrics, contended blind inputs,
   splitter routing, route-width boundaries, reset during traffic, and long
   backpressure runs.
10. Define traffic-safety support for set-and-forget `port` outputs: optional
    loss monitors detect a valid pulse ending without acceptance and report the
    responsible router/port IDs; topology optimization may bias these outputs
    toward low-contention paths without treating that bias as a delivery
    guarantee.
11. Add synthesis-oriented checks for generated module naming, timing-critical
   ready paths, inferred storage, and representative FPGA/ASIC toolchains.

## Release gates

- Pure SystemVerilog compatibility regressions demonstrate equivalent
  observable behavior before and after Pigen processing.
- Every intentional rejection of otherwise legal SystemVerilog is documented
  in `SPEC.md` and has a focused source-located diagnostic test.
- `make verify` passes from a clean tree.
- Strict compiler warnings remain errors and the block compiler passes static
  analysis.
- Every accepted construct is documented in `SPEC.md` and demonstrated by a
  `.pigen` source compiled with `pigen`.
- Generated SystemVerilog is deterministic for identical source and options.
