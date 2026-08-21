# README maintenance

`README.md` is the public statement of what Pigen is and where it is going. Keep
it readable as an introduction rather than turning it into a changelog or an
exhaustive specification.

The README must distinguish three layers which currently evolve at different
speeds:

1. The semantic idea: atomic ready/valid transfer and explicit transfer-type
   policy.
2. The syntax and features accepted by the production compiler, for which
   `USER_GUIDE.md` and the tests are the practical references during migration.
3. The intended v1 language surface and structured compiler architecture, for
   which `SPEC.md` is authoritative but which must not be presented as already
   implemented.

Update the README whenever a planned syntax becomes authoritative, the
production compiler changes architecture, or a major feature changes the
project-level story. Runnable snippets must use syntax accepted by the current
compiler unless a snippet is plainly labelled as intended syntax.

Keep the AI acknowledgement factual. David directs the project; agents are
used throughout engineering and documentation. Avoid unverifiable authorship
claims and avoid turning the acknowledgement into either marketing or an
apology.
