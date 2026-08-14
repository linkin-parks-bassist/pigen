#!/bin/sh
set -eu

pigen=${1:-./pigen}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric.sv"
grep -q 'module routed_fabric__fabric_router' "$temporary/fabric.sv"
grep -q 'ROUTE__source_a__tx__to_sink' "$temporary/fabric.sv"
grep -q 'route manifest: payload=PAYLOAD_W path_width=2' "$temporary/fabric.sv"
grep -q 'data-layout="tree-spring"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="unit" data-unit="source_a"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="router" data-router="r0"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="direct-link"' "$temporary/fabric.sv.svg"
grep -q 'data-kind="router-link"' "$temporary/fabric.sv.svg"
awk -f tests/check_fabric_svg.awk "$temporary/fabric.sv.svg"
awk -f tests/check_fabric_svg_labels.awk "$temporary/fabric.sv.svg"
if command -v xmlwf >/dev/null 2>&1; then
	xmlwf "$temporary/fabric.sv.svg"
fi
iverilog -g2012 -s fabric_block_tb -o "$temporary/fabric" \
	"$temporary/fabric.sv" tests/fabric_block_tb.sv
vvp "$temporary/fabric"
iverilog -g2012 -s fabric_skid_tb -o "$temporary/fabric-skid" \
	"$temporary/fabric.sv" tests/fabric_skid_tb.sv
vvp "$temporary/fabric-skid"
iverilog -g2012 -s fabric_router_tb -o "$temporary/router" \
	"$temporary/fabric.sv" tests/fabric_router_tb.sv
vvp "$temporary/router"

"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-repeat.sv"
cmp "$temporary/fabric.sv.svg" "$temporary/fabric-repeat.sv.svg"
"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-custom.sv" \
	--diagram "$temporary/network.svg"
test -f "$temporary/network.svg"
"$pigen" tests/fabric_block.pigen -o "$temporary/fabric-suppressed.sv" --no-diagram
test ! -e "$temporary/fabric-suppressed.sv.svg"

"$pigen" tests/multiple_fabrics.pigen -o "$temporary/multiple.sv"
test -f "$temporary/multiple.sv.first_diagram.svg"
test -f "$temporary/multiple.sv.second_diagram.svg"
if "$pigen" tests/multiple_fabrics.pigen -o "$temporary/multiple-custom.sv" \
	--diagram "$temporary/ambiguous.svg" 2>"$temporary/multiple.err"; then
	echo 'custom diagram path unexpectedly accepted for multiple fabrics' >&2
	exit 1
fi
grep -q 'requires exactly one fabric block' "$temporary/multiple.err"

"$pigen" tests/random_fabric_block.pigen -o "$temporary/random.sv"
iverilog -g2012 -s random_fabric_block_tb -o "$temporary/random" \
	"$temporary/random.sv" tests/random_fabric_block_tb.sv
vvp "$temporary/random"

"$pigen" tests/direct_fabric_block.pigen -o "$temporary/direct.sv"
if grep -q 'fabric_router\|PATH_W' "$temporary/direct.sv"; then
	echo 'direct-only fabric unexpectedly emitted routed infrastructure' >&2
	exit 1
fi
iverilog -g2012 -s direct_only -o "$temporary/direct" "$temporary/direct.sv"

"$pigen" tests/mixer_fabric_block.pigen -o "$temporary/mixer.sv"
grep -q 'parameter integer PATH_W = 7' "$temporary/mixer.sv"
grep -q 'ROUTE__input_left__samples__main = 110' "$temporary/mixer.sv"
awk -f tests/check_fabric_svg.awk "$temporary/mixer.sv.svg"
awk -f tests/check_fabric_svg_labels.awk "$temporary/mixer.sv.svg"
iverilog -g2012 -s mixer_network -o "$temporary/mixer" "$temporary/mixer.sv"

if "$pigen" tests/fabric_direct_error.pigen -o "$temporary/bad-direct.sv" \
	2>"$temporary/bad-direct.err"; then
	echo 'invalid direct fabric destination unexpectedly compiled' >&2
	exit 1
fi
grep -q 'direct fabric destination' "$temporary/bad-direct.err"
if "$pigen" tests/fabric_depth_error.pigen -o "$temporary/bad-depth.sv" \
	2>"$temporary/bad-depth.err"; then
	echo 'unsupported fabric buffer depth unexpectedly compiled' >&2
	exit 1
fi
grep -q 'fixes.*router_buffer_depth.*2 entries' "$temporary/bad-depth.err"

echo 'PASS: fabric lowering, diagrams, routing, and validation'
