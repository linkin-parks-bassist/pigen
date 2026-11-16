---
status: green
revised_at: "2026-11-14T16:55:51+11:00"
---

cores/spi/target.pigen implements an oversampled eight-bit, MSB-first, full-duplex SPI target in all four CPOL/CPHA modes. Native buf transfers queue TX and RX; ordinary always code synchronizes pins and shifts bytes. Continuous CS supports consecutive bytes. Each byte reserves TX on its first synchronized leading edge. A missing token sends zero and pulses underrun once; a token arriving later waits for the next byte. Full RX retains the old byte, drops the newest and pulses overrun.

SCK, CS and MOSI pass through two-stage synchronizers. Each physical SCK half-period and CS setup/high interval must last at least six system-clock periods; TX must be queued before the first leading edge and satisfy CPHA=0 first-bit setup. CPHA=0 exposes the first queued bit before consuming it, then samples leading/shifts trailing. CPHA=1 shifts leading/samples trailing. Physical metastability and I/O delays require board qualification. Edge conventions follow [Microchip AVR151](https://ww1.microchip.com/downloads/en/Appnotes/Atmel-2585-Setup-and-Use-of-the-SPI_ApplicationNote_AVR151.pdf).

miso and miso_enable are separate outputs for the FPGA I/O tri-state. Enable follows raw CS/reset so deselection/reset releases the bus immediately. Deselecting discards incomplete RX and already-reserved partial TX, while a queued future TX token remains. Reset flushes queues and state. CPOL/CPHA outside 0/1 fail simulation startup.

tests/cores/spi_target_tb.sv drives an independent asynchronous controller, exhausts all 256 byte values both ways under continuous CS, checks late TX, zero-fill, overrun retention, each one-through-seven-bit abort and reset recovery. All four modes pass jittered 61/73/107 ns half periods against a 10 ns system clock: 12 configurations, each with 269 checked RX bytes plus checked MISO replies. tests/cores/run.py compiles each configured native source, runs warnings-fatal Verilator lint and independent Icarus simulation, retaining CSV/VCD/log/hash evidence under build/cores/spi-target-mode*-half*. The gallery exposes source, diagrams, waveforms and comparisons.
