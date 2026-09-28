---
status: green
revised_at: "2026-09-14T20:38:04+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Unsized Pigen decimals are arbitrary-precision exact integers with no implicit signed family or hardware width, also used for Pigen type/transfer counts. Ordinary SV parameters/ranges retain SV unsized_integer rules. Literal domain is explicit policy input to one walker, independent of constant-only admissibility. Sized based literals retain normalized exact-width four-state raw bit vectors and need casts for Pigen arithmetic.

An exact literal-only +,-,*, unary+/- subtree stays exact until concrete numerical operation/cast/assignment. Exact data types contain the canonical integer ID and have no packed hardware width; packed selection/concat reject until represented. Exact-to-int/uint assignment may truncate at the typed boundary; exact-to-raw requires explicit policy. PIGEN_CONVERSION_EXACT_INTEGER represents exact-to-concrete and INTEGER_PROMOTION denotes lossless common signed/unsigned representation inside operations, never assignment family change. Sized based literals canonicalize by exact type and normalized LSB-first 0/1/x/z states, not spelling base or host capacity; occurrences retain separate expression identity/provenance. Ordinary SV unsized_integer is infrastructure, not source int[n].
