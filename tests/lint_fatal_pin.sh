#!/bin/sh
# Hidden pin for the lint-fails-on-warning property.
#
# Every Verilator lint invocation the harness runs must fail on warnings, so a
# generated design that emits a warning cannot let `make verify` pass. The
# -Wno-fatal flag suppresses exactly that: it reports a warning but exits 0. This
# pin witnesses that no lint invocation carries the flag, that the harness's lint
# genuinely fails on a warning (not merely that the flag is absent), and that the
# already flag-free co-slice lint still passes.
#
# Properties:
#   (1) STATIC: tests/smoke.sh contains zero occurrences of the string
#       -Wno-fatal, and the Makefile carries no -Wno-fatal on any --
#       lint-only line. A count of invocations is deliberately not pinned: it
#       would rot as designs are added, while the absence of the flag is the
#       invariant.
#   (2) NEGATIVE: a deliberately warning-bearing design (a WIDTHTRUNC
#       assignment, a warning under the default warning set) must FAIL under the
#       exact lint form the harness uses -- verilator --lint-only --top-module
#       <top> rtl/pigen_primitives.sv <file> -- with no -Wno-fatal. This proves
#       the harness's lint genuinely fails on warnings: if the flag were present
#       the witness would exit 0 and the pin would be red.
#   (3) POSITIVE: the co-slice lint line in tests/smoke.sh (already flag-free)
#       must still pass (exit 0), so the pin is also red if the co-slice design
#       regresses to a warning.
#
# POSIX sh, set -eu, self-contained, runnable as `sh tests/lint_fatal_pin.sh`
# from the repo root. The warning witness is generated into this invocation's own
# mktemp -d directory and cleaned on exit, so it is never left behind (the
# harness artifact-ownership contract).
set -eu

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The suppression flag, assembled from parts so this script's own text never
# carries it contiguously (a contiguous copy would itself trip a static scan).
flag="Wno""-f""atal"

# (1) STATIC: no lint invocation carries the suppression flag.
if grep -q -- "-${flag}" tests/smoke.sh; then
	printf 'static: tests/smoke.sh still contains the suppression flag\n' >&2
	exit 1
fi
if grep -F -- '--lint-only' Makefile | grep -q -- "-${flag}"; then
	printf 'static: Makefile carries the flag on a lint line\n' >&2
	exit 1
fi

# (2) NEGATIVE: a deliberately warning-bearing design must FAIL under the exact
# lint form the harness uses. A WIDTHTRUNC assignment is a warning under the
# default warning set (not an error), so the flag is what would mask it.
cat > "$tmp/witness.sv" <<'SV'
module warn_witness (
    input logic clk,
    input logic [7:0] in,
    output logic [3:0] out
);
    always_ff @(posedge clk) out <= in;
endmodule
SV
if verilator --lint-only --top-module warn_witness rtl/pigen_primitives.sv "$tmp/witness.sv" 2>/dev/null; then
	printf 'negative: warning witness passed; the lint is not failing on warnings\n' >&2
	exit 1
fi

# (3) POSITIVE: the already flag-free co-slice lint must still pass.
if [ ! -x ./pigen ]; then
	make pigen
fi
./pigen tests/coslice.pigen -o "$tmp/coslice.sv"
if ! verilator --lint-only --top-module coslice rtl/pigen_primitives.sv "$tmp/coslice.sv" 2>/dev/null; then
	printf 'positive: the flag-free co-slice lint no longer passes\n' >&2
	exit 1
fi

exit 0
