---
status: green
revised_at: "2026-11-14T17:56:28+11:00"
---

cores/uart/tx.pigen and rx.pigen implement LSB-first UART with DATA_BITS 5..9, PARITY 0=none/1=even/2=odd, STOP_BITS 1/2 and CLKS_PER_BIT >= 4. Defaults remain 8N1 at 16 clocks/bit. Native payload widths are bit[DATA_BITS]; TX constructs and shifts an exact-width frame containing start, data, optional parity and stops. The line idles high. Parameter and frame-size types use native int[32].

RX uses two synchronizer stages and validates a start near its midpoint, then samples data, parity and every configured stop bit. False starts produce no token. parity_error and framing_error pulse for detected malformed fields; bad frames are dropped. A held-low break produces one framing error, waits for the line to return high, then resumes start detection. A full output retains its old word, drops newest valid frames and pulses overrun. Corrupt frames do not count as overrun. Reset aborts both serializers and clears RX validity/errors.

tests/cores/uart_framing_tb.sv independently counts set bits for parity, decodes TX at pin centers, monitors complete stop duration and drives truly back-to-back RX frames. All 30 width/parity/stop formats run at divisors 4 and 7, exhausting all payload values (32..512). Tests also cover each malformed stop position, parity errors including a full output, false starts, break/recovery, held output stability, overrun and reset/restart. tests/cores/uart_tb.sv retains five default 8N1 divisor witnesses. Invalid width/parity/stop/divisor bounds are rejected. General frame definitions follow [Microchip USART frame formats](https://onlinedocs.microchip.com/oxy/GUID-383BD9F7-243B-427C-85D3-B47D840F7728-en-US-7/GUID-F84E04E8-3235-4E21-89A9-D1911E9B91DD.html); this RX explicitly validates every configured stop.

tests/cores/run.py retains configured native sources, emitted RTL, independent logs, CSV/VCD and hashes. The gallery has internal TX/RX views for default 8N1 and a 9O2 example, exposing frame/shift registers, bit motion, counters, parity state, stop index and error outputs. Baud mismatch tolerance, fractional divisors and physical metastability qualification are outside the tested profiles.
