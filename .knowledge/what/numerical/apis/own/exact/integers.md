---
status: "unverified"
created_at: "2026-09-13T15:04:54+10:00"
scope: "local"
source: "Full intrinsic-expression design/plan 2026-08-25 and semantic invariants; documentation audit Codex /root 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:38:04+10:00"
---

integer.h exposes decimal/u64 interning, negate/add/subtract/multiply, bounded shift/power, comparison/sign/zero/parity, checked conversions to size/u64 and signed/unsigned width queries. Exact integer identities belong to semantic model. Host extraction can fail without limiting the underlying exact value. Resource-bounded construction is distinct from language arithmetic meaning.

Canonical storage uses normalized sign plus little-endian uint32_t magnitude limbs, {negative, first_limb, limb_count} records and one model-owned shared limb arena. Zero is nonnegative with zero limbs; strip high zero limbs and compare normalized sign/magnitude for identity. Decimal parsing accumulates magnitude *10+digit, sign-aware add/subtract and schoolbook multiply use uint64_t temporaries, powers use exponentiation by squaring. Shift/power resource guards prove bounds before oversized allocation/interning. signed_width is minimum two's-complement width including negative asymmetry. Catalogue tests include 1208925819614629174706175 canonicalized with leading zeros, negative plus positive -> zero, square width 160, negative width 81, 1<<127 accepted at bound128 and 1<<128 rejected, 1 raised to huge exponent remains 1; malformed decimal, zero signs, subtraction crossing zero and multiplication signs. Construction limits are operational resource bounds, not implicit source-literal widths. Free record and limb arrays with semantic model. Historical owner files integer.h/c, ids.h, semantic.h/c, integer_test.c, Makefile, with integer-test semantic-test gate.
