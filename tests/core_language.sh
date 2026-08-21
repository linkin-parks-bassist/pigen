#!/bin/sh
set -eu

pigen=${1:-./pigen}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$pigen" tests/static_validate.pigen -o "$temporary/validate.sv" \
	2>"$temporary/validate.err"
test "$(grep -c 'warning: validate on an always-valid wire, reg, or logic signal has no effect' "$temporary/validate.err")" -eq 3
if grep -q '__pigen_force_valid' "$temporary/validate.sv"; then
	echo 'static validate unexpectedly emitted validity hardware' >&2
	exit 1
fi
iverilog -g2012 -s static_validate -o "$temporary/validate" "$temporary/validate.sv"

if "$pigen" tests/self_feedback_error.pigen -o "$temporary/feedback.sv" \
	2>"$temporary/feedback.err"; then
	echo 'direct signal self-feedback unexpectedly compiled' >&2
	exit 1
fi
grep -q 'cannot consume its own destination' "$temporary/feedback.err"

for kind in reg wire logic; do
	if "$pigen" "tests/${kind}_invalidate_error.pigen" \
		-o "$temporary/${kind}.sv" 2>"$temporary/${kind}.err"; then
		echo "invalidate(${kind}) unexpectedly compiled" >&2
		exit 1
	fi
	grep -q 'cannot make an always-valid wire, reg, or logic signal invalid' \
		"$temporary/${kind}.err"
done

echo 'PASS: core static-signal diagnostics'
