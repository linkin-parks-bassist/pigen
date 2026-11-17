---
status: green
revised_at: "2026-11-17T12:39:56+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A field name denotes the current stage's immutable incoming value on the right of <= and its outgoing value on the left. Reusing x across stages creates distinct buffer identities at each boundary. Writes in one stage do not affect other reads in that stage; repeated writes choose the last outgoing equation while all consuming inputs participate in the stage event.

Production src/resolve.c:resolve_pipeline creates a private field scope for each boundary and a separate stage-local scope over the incoming fields. Written fields receive new values; initialized unwritten fields are carried forward. One atomic semantic transfer produces the complete outgoing live field set and consumes all incoming field tokens, including overwritten fields. This retires unused incoming state and keeps packet fields synchronized under stalls. Ordinary buffered self-consumption remains an error outside pipeline versioning.

The final yield substitutes outgoing equations into its expression and writes the pipeline's buffered output directly, avoiding an extra yield register after the last authored stage. Nonfinal fields use buf storage; private type templates and final equation symbols are static implementation artifacts. Complete enclosing guards gate stages. Reset bindings traverse the canonical pipeline scope and all generated versions; pending bindings require matching lexical identity and are marked consumed.

Uninitialized field reads and yields reject, as do destinations outside the pipeline or partial-field writes. Each declared field has a fixed DATA type. Source syntax currently allows one declaration per field, stage declarations/assignments and a final yield.


tests/pipeline_packet_test.py passes fourteen profiles of 257 packets covering repeated names, immutable same-stage snapshots, carry, last writes, retirement of overwritten values, atomic joins, later external inputs, 8-bit signed and 129-bit DATA, packed records, initialized locals, type inquiries and extra yield inputs. Guards include X/Z; scoreboards exercise stalls and queued reset. A separate 128-packet run sustains one packet per cycle. Merged/split designs preserve data with exact two/three-stage latency plus the explicit outer output buffer. Eleven negative cases preserve definite initialization, destination and ordinary self-consumption rules.

The complete driver passes normally (/tmp/pigen-pipeline-final-focused.log) and under compiler ASan/UBSan with leak detection (/tmp/pigen-pipeline-final-sanitizers.log). Procedural exits and lexical resets pass normal and sanitizer liveness suites (/tmp/pigen-pipeline-liveness.log; /tmp/pigen-pipeline-final-liveness-asan.log). Eighteen qualified/generated all-family profiles pass in /tmp/pigen-final-generated-pipelines-vqr4nytv. Generated-scope C models and existing pipeline syntax/scope tests also pass.

The broader repository gate passes with the separately checked native-protocol target excluded (/tmp/pigen-pipeline-verify.log). Final focused tests and sanitizers cover subsequent localized refinements. Pre-quarantine pipeline checkpoint compiler SHA-256 is 566ed4f7fb838029ee923b0decff503be5e15ee9145264e7103fafc1f743dbd2. Current complete README module/fabric snippets pass compilation and fatal lint (/tmp/pigen-quarantine-readme-j3adkrrm). Native whole-interface fabric integration witnesses are quarantined; the active pipeline liveness suite retains thirty-six direct/child profiles. Native AXI acceptance is owned separately by what/is/the/native/axi/acceptance/status.md.
