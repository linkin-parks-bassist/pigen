---
status: green
revised_at: "2026-11-21T22:55:29+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Use one concatenated/co-sliced transfer: `{hi, lo} <= packet;` or `packet <= {hi, lo};`, or the readable `transfer begin` group whose members normalize to that concatenation. Braces are packed bit streams, leftmost most significant. The whole source is resized to the destination aggregate before splitting, so item counts and member widths may differ. All members advance atomically; repeated projections consume the complete base once. Separate statements are separate consumers.
