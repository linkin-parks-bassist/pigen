---
status: green
revised_at: "2026-09-13T15:07:28+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Approved range-derived examples: uint[8]+uint[8] -> uint[9]; int[8]+int[8] -> int[9]; int[8]+uint[8] -> int[10]; uint[8]-uint[8] -> int[9]; negation of either eight-bit integer -> int[9]; two uint[8] multiply -> uint[16]; two int[8] multiply -> int[16]; uint[8]+1 -> uint[9]; uint[8]*3 -> uint[10]. `(a+b)*c` for three int[8] is int[17], with only final assignment truncation to int[16].
