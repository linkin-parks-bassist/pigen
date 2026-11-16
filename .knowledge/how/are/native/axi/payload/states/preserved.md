---
status: green
revised_at: "2026-11-16T14:48:34+11:00"
---

Native DATA retains its declared signedness and state domain. resolve.c:protocol_bits_type derives channel-record and unsigned sideband state from the DATA type; DATA fields retain their own semantic types. RTL types, boundary pins, storage PAYLOAD_T and projection aliases carry those states through lowering. Equal-domain copies preserve X/Z; actual four-to-two conversions follow how/are/four-state/values/converted/to/two-state/types.md rather than silently clearing working views.

tests/native_protocol_test.py:four_state_channel_tests checks 40 profiles: all families, all three storage kinds, direct/internal routed child paths and native aliases at 32 bits, plus 128-bit Stream/full AXI. signed_four_state_channel_tests adds ten signed profiles across all families and the two wide families. Signed probes compare native field sign tests and byte slices against independent SV expressions on the physical pins.

Each field receives X in its low bit every seventh record and Z in its high bit every eleventh record, with field-specific phase. Controls remain known. Four-state expected/held records and case inequality check exact transport, observations, order, capacity, stalls and queued reset. These are channel-preservation profiles; unknown memory-mapped descriptors do not establish legal transaction or address-decoder behavior.

All 50 profiles pass normal Icarus simulations and compiler ASan/UBSan with fatal design lint. Evidence: /tmp/pigen-four-state-channels.log, /tmp/pigen-four-state-channels-asan.log, /tmp/pigen-signed-four-state.log and /tmp/pigen-signed-four-state-asan.log. The full repository gate containing these profiles passes (/tmp/pigen-axi-acceptance-verify.log).
