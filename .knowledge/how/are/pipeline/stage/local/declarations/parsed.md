---
status: green
revised_at: "2026-11-17T11:40:37+11:00"
---

src/syntax.c parses stage-local declarations separately from travelling packet fields. A stage local is static: omission selects WIRE, and an explicit static transfer type is accepted after the data type. Buffered transfer annotations reject. This default is independent of data-type omission policy, so a numerical uint[N] temporary is valid without a separate transfer annotation.

The stage parser publishes a SIGNAL_DECLARATION with a concrete static transfer occurrence and parses its initializer in combinational mode. It rejects trailing unparsed tokens. WIRE initializers are lexical combinational expressions, expanded through dependent temporaries into outgoing field equations so their consuming inputs share the stage handshake. Type/shape resolution runs over the incoming stage-version scope. Self-referential initializers and uninitialized field reads reject; pure type/shape inquiries remain available. Declarations cannot shadow pipeline fields or other active stage locals. Pipeline fields retain their separate rule: omitted BUF and explicit BUF are equivalent; other transfer types reject.

parse_pipeline pushes the field-name table before reading the body and pops it on both success and failure. Separate pipelines can therefore reuse field names without being mistaken for nested scopes, and successful parsing does not retain field-name allocations. Enclosing begin/end scanners in syntax and specialization recognize endpipeline as closing the pipeline's begin.

Evidence: tests/generate_semantic_test.c exercises numerical stage locals with omitted and explicit WIRE types beneath generated scopes, excludes them from reset actions, and resolves loop-dependent widths. tests/native_array_test.py declares the same first/final field names in independent per-channel pipelines. The separate stage-law owner is what/is/the/pipeline/stage/execution/laws/owner/seam.md; it does not replace production declaration resolution.
