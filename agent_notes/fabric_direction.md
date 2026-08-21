# Fabric language direction

## Inline ownership (2026-08-20)

Fabrics should be inline blocks inside a parent module.  Their endpoints are
the input and output ports of child-module instances in that same parent.  A
fabric is not conceptually a separate generated design unit with flattened
endpoint names; it is structured hookup and interconnect inside the module
which owns the instances.

The production compiler's top-level `fabric` grammar and required shared
`PAYLOAD_W` parameter are implementation-era syntax, not the intended endpoint.
`SPEC.md` now defines the inline, width-inferred target. When the structured
compiler reaches fabrics, replace the old grammar and lowering cleanly. Do not
retain top-level and inline fabric dialects together.

## Endpoint and width semantics

A fabric connection resolves an output-port identity on one child instance to
an input-port identity on another. Width and data type come from those
resolved ports and are checked per connection; users should not repeat a
fabric-wide payload-width parameter. This requires instance, port, and type
identity in the semantic model before fabric lowering. A textual
`instance.port` path is only syntax.

Endpoints remain blind to their peers and to the network.  Their contract is
payload, valid, and ready.  Relative route state, arbitration state, topology,
and buffering are internal to the fabric and must not appear in child-module
interfaces.

## Generated network

Routed connections should retain the useful existing architecture:

- a deterministic balanced network whose router depth grows logarithmically
  with endpoint count;
- small three-port routers using compact relative route decisions at each hop;
- skid-like two-entry buffering at endpoints and router ingress to break ready
  paths while sustaining pipelined throughput;
- backpressure across the complete route;
- arbitration where routes contend;
- reachability proofs, route manifests, RTL, and SVG derived from one topology
  model;
- direct exclusive connections which bypass the routed network.

Round-robin is the current arbitration policy.  Priorities are planned, and
connection tiers already express some of the intended source notation, but the
priority semantics and their relationship to topology objectives are not yet
settled.  Do not bake a distributed policy into unrelated compiler layers.

## Child instantiation syntax

David intends a cleaner, struct-like replacement for ordinary Verilog module
instantiation syntax.  Its grammar has not yet been specified.  Inline fabric
syntax should be designed around resolved instance and port objects so that the
surface instantiation syntax can change independently.  Do not infer a grammar
from README examples or make fabric resolution depend on rendered Verilog
instance text.
