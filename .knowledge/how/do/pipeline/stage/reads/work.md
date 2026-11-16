---
status: green
revised_at: "2026-11-17T10:19:14+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The language contract gives every stage an immutable incoming packet and a separate outgoing packet. A field name such as x denotes that stage's incoming value when read, while x <= expression defines its outgoing value for the following stage. Reusing x across stages expresses successive transformations with one namespace and physically distinct stage registers. Reads within one stage do not see earlier writes in that same stage. The first stage initializes fields from enclosing signals or stage-local combinational values. External consuming inputs participate atomically under the complete enclosing guard.

Production does not yet implement this contract for repeated field names. A three-stage pipeline containing x <= source; then x <= x + 8'd1; then x <= x + 8'd2; is rejected by ./pigen with "buffered transfer cannot source its destination". The reproducer is /tmp/pigen-same-x-2my2foxi/pipeline.pigen. src/resolve.c resolve_pipeline declares each field once in pipeline_scope and resolves stage assignments against that shared signal; analyze_transfer consequently diagnoses the read/write as self-consumption. This evidence establishes rejection before RTL emission, so the per-stage runtime behavior remains unverified.

Stage boundaries also fail to establish the specified packet transformations in a direct production witness. Compiling the distinct-name increment_pipeline with two stages (first <= incoming; then second <= first + 32'd1;) produces byte-identical SV to placing both assignments in one stage (/tmp/pigen-stage-boundary-thczxitw/split.pigen and merged.pigen). The emitted design has ordinary first, second and yield-output buffers, with independent transfers between them. resolve_pipeline creates scopes for stages but resolves their assignments as ordinary transfers, so this witness's storage and timing follow field dependencies rather than authored stage boundaries. This result concerns these straight-line assignments; stage-local scope and pipeline reset/output identities still have effects.

The next check is to give incoming and outgoing field versions distinct semantic identities, preserve same-stage incoming reads and carry-forward across boundaries, then simulate the same-name example under downstream stalls. Existing distinct-name pipeline tests do not establish this defining behavior.
