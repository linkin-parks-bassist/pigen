---
status: green
revised_at: "2026-11-19T08:22:43+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Production does not implement transfer-begin blocks: the structured frontend rejects them as unsupported statements in a clocked process. Use the equivalent co-sliced assignment today. In the README, uint[32] buf output_packet and uint[32] fifo[8] history receive {output_packet, history} <= {result, result}; both destinations accept together while result is consumed once. Independent readers may drain the two storage objects at different rates. A complete module containing this form compiles and passes fatal Verilator lint.

Target grammar is `transfer begin transfer-member+ end`, with each direct member `destination-expression <= source-expression;`. It is exact sugar for concatenating destinations and sources in member source order. `transfer begin output_packet <= result; state_mem[handle] <= next_state; end` equals `{output_packet, state_mem[handle]} <= {result, next_state};`. The group owns one complete guard, aggregate width check, readiness/validity decision and fire event; all members update or none do. Individual member widths and arities may differ; only flattened aggregate widths match. The first version permits transfer members only; control flow surrounds the block. Signal reads needed by lvalue addresses/selects, such as handle, participate in validity/ownership and deduplicate with other projections of their complete base. Constants/statics retain their normal laws. Recognition is token-based, insensitive to whitespace/comments/newlines, and contextual only in the complete transfer-begin-end form inside a supported clocked process. Outside it transfer remains an ordinary SV identifier.


The block grammar remains a target, not current compiler acceptance. Implementing it requires contextual syntax recognition and normalization to the existing atomic concatenated transfer before semantic ownership analysis.
