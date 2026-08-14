#!/bin/sh
set -eu

tool=${1:-./pigen}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

"$tool" tests/transport_syntax.pigen -o "$temporary/transport_syntax.sv"
iverilog -g2012 -o "$temporary/transport_syntax" \
	rtl/pigen_primitives.sv "$temporary/transport_syntax.sv" tests/transport_syntax_tb.sv
vvp "$temporary/transport_syntax"

cp tests/sv_compat_syntax.pigen "$temporary/sv_original.sv"
iverilog -g2012 -o "$temporary/sv_original" \
	"$temporary/sv_original.sv" tests/sv_compat_syntax_tb.sv
vvp "$temporary/sv_original" >"$temporary/sv_original.out"
"$tool" tests/sv_compat_syntax.pigen -o "$temporary/sv_generated.sv"
iverilog -g2012 -o "$temporary/sv_generated" \
	"$temporary/sv_generated.sv" tests/sv_compat_syntax_tb.sv
vvp "$temporary/sv_generated" >"$temporary/sv_generated.out"
cmp "$temporary/sv_original.out" "$temporary/sv_generated.out"

"$tool" tests/slice_width_error.pigen -o "$temporary/slice_width_error.sv"
iverilog -g2012 -s slice_width_error -o "$temporary/slice_width_error" \
	rtl/pigen_primitives.sv "$temporary/slice_width_error.sv" \
	>"$temporary/width.out" 2>"$temporary/width.err"
if vvp "$temporary/slice_width_error" >>"$temporary/width.out" \
	2>>"$temporary/width.err"; then
	echo "expected sliced transport width mismatch to fail elaboration/simulation" >&2
	exit 1
fi
grep -q 'Pigen transfer aggregate width mismatch' \
	"$temporary/width.out" "$temporary/width.err"

if "$tool" tests/projected_buffer_destination_error.pigen \
	-o "$temporary/projected_buffer_destination_error.sv" \
	2>"$temporary/projected.err"; then
	echo "expected projected buffered destination to fail" >&2
	exit 1
fi
grep -q 'buffered transport destination must be written as a complete value' \
	"$temporary/projected.err"

if "$tool" tests/slice_fanout_error.pigen -o "$temporary/slice_fanout_error.sv" \
	2>"$temporary/fanout.err"; then
	echo "expected separate slices of one buffered source to fail ownership checking" >&2
	exit 1
fi
grep -q 'buffered transport value has more than one consumer' "$temporary/fanout.err"

if "$tool" tests/wire_transfer_destination_error.pigen \
	-o "$temporary/wire_transfer_destination_error.sv" 2>"$temporary/wire.err"; then
	echo "expected procedural wire transport destination to fail" >&2
	exit 1
fi
grep -q 'wire cannot be a procedural transport destination' "$temporary/wire.err"

echo "PASS: transport syntax positive and negative matrix"
