# Input boundaries: payload shape and transfer contract

## Design direction (2026-08-20)

Module inputs should be generic transfer endpoints.  At the semantic boundary,
an input says what payload the module receives; for now this is principally its
packed width.  The module body should not need to know whether the producer is
realised as a `wire`, `reg`/`logic`, `buf`, `port`, `fifo`, or `skid`.

An input may optionally name a transport kind.  When it does not, the emitted
module interface must still expose the complete ready/valid contract: payload
and valid enter the module, and ready leaves it.  The module accepts a transfer
only on the handshake.  Consequently, anything obeying that contract can be
connected without changing the receiving module.  Degenerate producers remain
possible by tying their handshake signals to the appropriate constants at the
connection or lowering boundary; degeneracy is not part of the receiver's
semantics.

The important separation is therefore:

- the input endpoint's semantic contract is always an atomic ready/valid
  transfer of a payload;
- a transport kind, when written, describes an interface realisation or
  adapter choice rather than changing how the receiving module consumes the
  input;
- storage, pulse, timing-path, and always-valid properties belong to the
  producing side or to explicit boundary machinery, not to knowledge imported
  into the receiver;
- transport identity and payload shape remain explicit semantic data even when
  the surface declaration omits a transport kind.

This contract is now normative in `SPEC.md`. The production compiler still
implements the earlier model, where an ANSI input port's kind selects
kind-dependent local ready/valid behaviour. Do not preserve both
interpretations during cutover: update syntax, semantic invariants, emitted
interfaces, examples, and tests together.

## Architectural consequence

The replacement compiler should not represent a module input merely as an
ordinary local transport declaration with a direction bit.  It needs a
boundary endpoint object whose invariant is the uniform transfer contract,
separate from any selected adapter or storage realisation.  In particular,
validity, readiness, and consumption analysis inside the receiving module must
depend on the endpoint contract, not branch on the peer's transport kind.

This separation also aligns with source-blind fabric endpoints: fabric and
direct hookup can reason about one payload/valid/ready protocol while endpoint
realisation remains local.

## Longer-horizon thought

There may eventually be a useful width-agnostic or width-following layer in
which most objects are parameterised and widths propagate through connected
transforms.  For now, inputs still specify width explicitly.  Keep payload
shape represented structurally so later width constraints can replace eager
concrete-width requirements without redesigning transport identity or the
handshake model.

## Decisions deliberately left open

This note records direction, not surface grammar.  Before implementation,
David should choose:

- the exact spelling of a kindless input and of its optional transport-kind
  annotation;
- whether an explicit input kind requests storage inside the receiver, an
  adapter immediately outside it, or some other interface realisation;
- the corresponding rule for module outputs, which this input-focused thought
  does not settle;
- where direct instantiation owns the constant ties or adapters when no fabric
  performs the hookup;
- whether payload declarations remain full SystemVerilog packed types or are
  intentionally reduced to width expressions before the later width-following
  design is undertaken.
