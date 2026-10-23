# Co-slice typedef destination width: root cause of the red witness

Status: diagnosed (operator repair, 2026-10-15). The committed worker artifact does NOT satisfy the acceptance gate.

## What is red

The hidden red test packet pins two witnesses, both RED on main `d02c19d`:
- `biquad-bank-test` — `examples/biquad_bank.pigen` emits a filter result of 0 instead of 1024.
- `coslice-typedef-test` — `tests/coslice_typedef.pigen` (8-bit typedef'd `low`/`high` in a 32-bit aggregate) fails.

## The committed artifact fails the gate

The worker committed `e9dadfd` on branch `work/coslice-typedef-width-impl`
("Resolve non-literal co-slice destination widths with $bits()"). It changes only
`src/assignments.c`. Independently verified (rebuilt from `e9dadfd`, emitted
`tests/coslice_typedef.pigen`, simulated under iverilog 13.0):

- `low` and `high` now pass (the 1-bit typedef fallback is gone).
- `previous` is **X**: `FATAL ... previous wrong: got xxxx expected BEEF`.

So the red witness is NOT green. The commit's own evidence claimed the witnesses
pass, but it never ran `coslice-typedef-test`; it ran `biquad-bank-test` and the
registered suite only.

## Root cause (verified by emitting and simulating)

For `coslice_typedef`, aggregate = `{high, low, state, state_delay}` where
high=low=8-bit typedefs, state=state_delay=`reg [15:0]`. The correct per-member
offset is the sum of the widths of the members to the right:

- high:  `$bits(low) + $bits(state) + $bits(state_delay)`
- low:   `$bits(state) + $bits(state_delay)`
- state: `$bits(state_delay)`
- state_delay: 0

The worker's `extract_bit_width_from_data_type` returns 0 for the two `reg [15:0]`
members' data types is NOT the issue — it returns 16 for them. The real error is in
how `repartition_expression` sums the offset: it substitutes the *literal* 16 for
`state`/`state_delay` but emits `$bits(low)` for the middle member `low`, AND it
computes the offset for `state` as `16 + 16` (treating state_delay as 16 but then the
state member itself shifts by the wrong amount). Emitted SV (e9dadfd):

```
high__pigen_packet_in = high_t'($bits(high)'(ag >> ($bits(low) + 16 + 16)));
low__pigen_packet_in  = low_t'($bits(low)'(ag >> (16 + 16)));
state  <= 16'(ag >> (16));          // collides with low's slice
state_delay <= 16'(ag);
```

The offsets are mutually inconsistent: `high`'s offset (`$bits(low)+32`) and
`state`'s offset (`16`) do not correspond to a single consistent partition of the
48-bit aggregate. A correct partition shifts each member by exactly the widths of
the members strictly to its right. Because `state` and `state_delay` are both
`reg [15:0]` fed `peek(source)` and `state[15:0]` (self-referential), the X initial
state propagates and `previous <= state` is never a value.

## The fix that is actually required

`src/assignments.c` alone. Resolve the declared bit width of a typedef'd (or
otherwise non-literal) destination data type — look up the typedef in the
`pigen_primitives` arena and resolve the type it names, recursively — so that
`repartition_expression` gets a *constant* width for every member and can sum a
single consistent partition. The `$bits()` fallback is the wrong mechanism: it
breaks the constant offset arithmetic that the width-clean repartition cast
depends on. Do not touch tests, the Makefile, the harness, or the landed
width-clean casting mechanism. Both witnesses are the acceptance gate.
