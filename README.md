# Pigen

This directory is the clean-room successor to `../pigen`.

The intended implementation is a normal C program that parses a small
SystemVerilog extension and lowers it to ordinary SystemVerilog.  The existing
Python project is reference material only: its tested elastic-buffer behavior
is useful, but its frontend and architecture are not being continued here.

The language contract is in [`SPEC.md`](SPEC.md); the implementation roadmap
is in [`PLAN.md`](PLAN.md).

Build and run the prototype with:

```sh
make
./pigen example.pigen              # writes example.sv beside the input
./pigen example.pigen -o out.sv    # explicit destination
```

Elaborated designs use the companion SystemVerilog library at
[`rtl/pigen_primitives.sv`](rtl/pigen_primitives.sv).  It contains readable
`pigen_buf`, `pigen_fifo`, `pigen_skid`, and `pigen_port` implementations.

Pigen transport actions are atomic ready/valid transfers.  Use `valid(x)` and
`ready(x)` to inspect a transport value, or `accepts(destination, source)` as
the shorthand for `ready(destination) && valid(source)`.  Buffered values may
be explicitly cleared inside an `always_ff` block with `invalidate(value);`
(drop one offered item) or `flush(value);` (empty all buffered items).

Inside `always_ff`, `if (destination <= source)` both tests the same condition
as `accepts(destination, source)` and performs that transfer when accepted.

## Make pipelines feel like plumbing

Declare the places data can wait, then connect them.  Each `<=` is an atomic
ready/valid transfer: it waits when the next stage is full and naturally
pushes back through the whole chain.  No hand-written `valid`, `ready`, or
enable plumbing required.

```systemverilog
module easy_pipeline
    (
        input logic clk,
        input logic reset,
        input buf [31:0] incoming,
        output buf [31:0] outgoing
    );

    buf  [31:0] decode;
    fifo [31:0][8] queue;

    always_ff @(posedge clk)
    begin
        decode <= incoming;
        queue <= decode;
        outgoing <= queue;
    end
endmodule
```

The same notation handles joins: all buffered inputs are consumed together,
never half a packet at a time.

```systemverilog
always_ff @(posedge clk)
begin
    // Fires only when both operands are available and sum can accept.
    sum <= left + right;

    // Test-and-transfer in one line.
    if (result <= sum)
        completed <= 1'b1;
end
```

Guards simply decide when a route is live; the source stays put until that
route can actually accept it.

```systemverilog
always_ff @(posedge clk)
begin
    if (launch)
        request_queue <= request;

    if (flush_requested)
        flush(request_queue);
end
```

FIFO declarations always spell payload before depth: `fifo[7:0][4] queue;`.
The earlier depth-before-payload spelling is rejected.

At a module boundary, a transport declaration expands to the payload plus
ordinary `name_valid` and `name_ready` ports.  The compiler's `__pigen_*`
names are private implementation details and never part of the public module
interface.

Run `make waveform` for the first ready/valid pipeline proof of concept.  It
checks ordered delivery through three `pigen_buf` instances and writes
`examples/buf_pipeline.vcd`, viewable with `gtkwave examples/buf_pipeline.vcd`.

`make compiler-waveform` elaborates and simulates
`examples/compiler_pipeline.pigen`.  It validates the generated atomic join
for `sum <= a_value + b_value` and writes `examples/compiler_pipeline.vcd`.

`make fifo-waveform` elaborates `fifo[7:0][4]`, verifies that its first pushed item
is valid even while downstream is stalled, then fills and drains it under
Verilator while writing `examples/fifo_pipeline.vcd`.

`make skid-waveform` does the corresponding two-entry `skid` demonstration and
writes `examples/skid_pipeline.vcd`.

`make port-waveform` drives a sparse `input buf` stream into a `port`, checks
the one-cycle pulse behavior, and writes `examples/port_pipeline.vcd`.

`port` is also the timing-friendly bridge for inferred synchronous RAMs.  A
guarded write into an internal port emits an unconditional payload register
write and a separately gated valid pulse, so this source remains a natural
BRAM read shape:

```systemverilog
if (read_enable)
    bram_port <= mem[read_address];
```

Conversely, an ordinary memory write can consume an input transport directly:
`mem[write_address] <= data_in;`.  Pigen gates that write on `data_in` valid
and drives its ready handshake.  `make bram-waveform` exercises both paths and
writes `tests/port_bram.vcd`.

`make guarded-waveform` shows a guarded transport assignment holding its input
under a disabled guard, then transferring ordered values when enabled.

`make output-waveform` drives an `input buf` port through a compiler-lowered
`output buf` port under backpressure, checks ordered delivery, and writes
`examples/output_pipeline.vcd`.

## Examples and waveforms

| Example | What it demonstrates | Artifact |
| --- | --- | --- |
| [buf pipeline](examples/buf_pipeline.pigen) | Three explicit elastic stages under backpressure | [VCD](examples/buf_pipeline.vcd) |
| [expression pipeline](examples/compiler_pipeline.pigen) | Compiler-lowered `sum <= a_value + b_value` join | [VCD](examples/compiler_pipeline.vcd) |
| [buffered join pipeline](examples/join_pipeline.pigen) | Atomic join of two independently pulsed `input buf` values | [VCD](examples/join_pipeline.vcd) |
| [FIFO pipeline](examples/fifo_pipeline.pigen) | `fifo[7:0][4]` fill, backpressure, and drain | [VCD](examples/fifo_pipeline.vcd) |
| [skid pipeline](examples/skid_pipeline.pigen) | Exact two-entry skid capacity | [VCD](examples/skid_pipeline.vcd) |
| [port pipeline](examples/port_pipeline.pigen) | Sparse input becomes one-cycle valid pulses | [VCD](examples/port_pipeline.vcd) |
| [BRAM port test](tests/port_bram.pigen) | Input-port memory write plus separate-address delayed port read | [VCD](tests/port_bram.vcd) |
| [guarded pipeline](examples/guarded_pipeline.pigen) | `if (enable)` gates an elastic transfer | [VCD](examples/guarded_pipeline.vcd) |
| [output port pipeline](examples/output_pipeline.pigen) | Buffered module-boundary transfer under backpressure | [VCD](examples/output_pipeline.vcd) |

Run every compiler and simulation check with `make verify`.  View a VCD with
`gtkwave examples/fifo_pipeline.vcd`, substituting any listed waveform.
